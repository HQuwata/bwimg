//=================================================================================================
// ファイル名:	blp2bmp.cpp
// 主題:		blp を bmp に変換
// 履歴:		2016.01.12	H.Kuwata	新規作成
//=================================================================================================
//=================================================================================================
// 参照するファイル
//=================================================================================================
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <sys/stat.h>
#include <dirent.h>

#include <png.h>

#define __Cygwin64
#ifdef __Cygwin64
#include <stdlib.h>  //strtol strtof
#endif

#pragma pack(1)

//=================================================================================================
// 定義・定数・グローバル変数等
//=================================================================================================
using namespace std;

#define	byte	unsigned char
#define	word	unsigned short
#define	sbyte	char
#define	sword	short
#define	ubyte	unsigned char
#define	uword	unsigned short

#define	_BIT0	0x01
#define	_BIT1	0x02
#define	_BIT2	0x04
#define	_BIT3	0x08
#define	_BIT4	0x10
#define	_BIT5	0x20
#define	_BIT6	0x40
#define	_BIT7	0x80

#define	_BIT8	0x0100
#define	_BIT9	0x0200
#define	_BIT10	0x0400
#define	_BIT11	0x0800
#define	_BIT12	0x1000
#define	_BIT13	0x2000
#define	_BIT14	0x4000
#define	_BIT15	0x8000

#ifdef Compress_Format

圧縮方法
	グラフィックスプレーンの横方向ランレングス圧縮
	統計頻度でソートしたパレット変換テーブルを持つ
	最大１２８色まで

	BLP フォーマット
		圧縮データの画素コードはパレットテーブルの参照番号をあらわす。
		画像メモリにはパレットテーブルの１バイトコードが展開される。
		パレットデータを別に持つ必要がある場合や、パレットコードが固定
		している場合に使用する。

	BCP フォーマット
		圧縮データの画素コードはパレットコードそのものとなり、パレット
		テーブルは直接パレットレジスタに展開されるものである。
		パレットデータごと画像圧縮する場合に使用する。
		エンコード時にはパレットコードを統計検索して再配置してしまう
		モードと、パレットを固定するモードとがある。ただし、パレット
		テーブルは統計圧縮に関与するため、パレット固定モードは１６色以下
		にしか使用しない。


ファイルフォーマット

	1. ヘッダー情報 (12 bytes)
	struct Header {
		char  File_ID[4] = 'BMPC';
		word  WX,WY;			/* Image Size */
		word  Colors;			/* Palette Stat Table Count */
		word  Status;			/* Status & Control bit flags */
			/* _bit0 : skip trace (status)         現在意味なし */
			/* _bit1 : word palette (status)       パレットデータは word (bcp format) */
			/* _bit2 : or put procedure (control)  0を透明として重ね書き(OR書き) */
			/* _bit3 : double mag (control)        2倍に拡大して表示 */
			/* _bit4 : even palette mode (control) 偶数パレットに展開 */
	}

	2. パレットデータ (Header.Colors * (Header.Status.bit1 ? 2 : 1) bytes)
	byte  Palette[Header.Status.Colors];
		パレットデータはパレットコードの統計テーブルである。
     or 
	word  Palette[Header.Status.Colors];
		パレットデータはカラーコードである。

	3. 圧縮データ (variable)
	byte  Press_Data[];


圧縮データフォーマット

	Alone Pattarn     単一に現われるパターン
		1ppppppp
			p : 0-127  color = Palette[p];

	Continuas Pattarn 連続するパターン
		0lllpppp [l1] [l2] [l3] [p]
			p : 1-12  color = Palette[p-1];  !!!
			    0,   color = Palette[次の 1byte{0-127}];
			    13,   color = 展開画面の上と同じ
			    14,   color = 展開画面の左上と同じ
			    15,   color = 展開画面の右上と同じ

			l : 1-7    length = l+1; {2-8}
			    0      length = 次の 1byte {9-255};
			            if 次の 1byte == 0; length = その次の 1byte + 256; {256-511}
			            if 次の 1byte == 1; length = その次の 1byte + 512; {512-767}
			            if 次の 1byte == 2; length = その次の 1byte + 768; {786-1023}
			            if 次の 1byte == 3; length = 1024;

		
#endif
//=================================================================================================
// Section コマンドラインオプション解析
//=================================================================================================
struct ArgOption
{
	bool        isSet;
	enum ValueType
	{
		vNon,
		vBool,
		vInteger,
		vString,
		vFloat,
	}			vType;
	bool        bValue;
	int         iValue;
	std::string sValue;
	float       fValue;

	ArgOption() : isSet(false), vType(vNon) {}
	ArgOption(bool _default) : isSet(false), vType(vBool) {
		bValue = _default;
	}
	ArgOption(int _default) : isSet(false), vType(vInteger) {
		iValue = _default;
	}
	ArgOption(const char* _default) : isSet(false), vType(vString) {
		sValue = _default;
	}
	ArgOption(float _default) : isSet(false), vType(vFloat) {
		fValue = _default;
	}
	ArgOption* Key() {
		if (vType == vBool) {
			if (!isSet) {
				bValue = !bValue;
				isSet = true;
			}
			return NULL;
		}
		else {
			return this;
		}
	}
	ArgOption* Param(const std::string param) {
		switch (vType) {
			case vInteger:
#ifdef __Cygwin64
				iValue = ::strtol(param.c_str(),NULL,10);
#else
				iValue = std::stoi(param);
#endif
				isSet = true;
				break;
			case vString:
				sValue = param;
				isSet = true;
				break;
			case vFloat:
#ifdef __Cygwin64
				fValue = ::strtof(param.c_str(),NULL);
#else
				fValue = std::stof(param);
#endif
				isSet = true;
				break;
			case vBool:
			default:
				return NULL;
		}
		return NULL;
	}

	bool Check() {
		if (vType == vBool) {
			return bValue;
		}
		else {
			return isSet;
		}
	}
	//! @brief	アクセサ
	operator const int() {
		if (vType == vInteger) {
			return iValue;
		}
		else if (vType == vFloat) {
			return static_cast<int>(fValue);
		}
		else {
			return 0;
		}
	}
	operator const std::string() {
		if (vType == vString) {
			return sValue;
		}
		else {
			return "";
		}
	}
	operator const float() {
		if (vType == vInteger) {
			return static_cast<float>(iValue);
		}
		else if (vType == vFloat) {
			return fValue;
		}
		else {
			return 0;
		}
	}
		
};

typedef std::map<std::string,ArgOption> ArgOptions;

void ArgAnalyzer(int argc, char* argv[], std::vector<std::string>& filelist, ArgOptions& options)
{
	ArgOption* checked = NULL;
	for (int ccarg = 1; ccarg < argc; ccarg++) {
		std::string argpath = argv[ccarg];  //読み込むファイルの指定

		// オプションチェック
		if (options.find(argpath) != options.end()) {
			checked = options[argpath].Key();
		}
		else if (checked != NULL) {
			checked = checked->Param(argpath);
		}

		// ワイルドカード対応 ... mac/linux ではシェルがやってくれるから無意味だった!!
		else if (argpath.find('*') != std::string::npos) {
			std::regex reass("\\*");
			std::regex redot("\\.");
			if (std::regex_match(argpath, reass )) {
				std::regex_replace(argpath, reass, ".*") ;
			}
			if (std::regex_match(argpath, redot )) {
				std::regex_replace(argpath, redot, "\\.") ;
			}

			std::regex re(argpath);   //"k.*\\.csv"

			DIR *dp = opendir(".");
			struct dirent *dirst;
			while((dirst = readdir(dp)) != NULL)
			{
				std::string filepath = dirst->d_name;
				if (!std::regex_match(filepath, re)) {
					continue;
				}
				//std::cout << filepath << std::endl;
				filelist.push_back(filepath);
			}
			closedir(dp);
		}
		else {
			filelist.push_back(argpath);
		}
	}
}

//=================================================================================================
// Section BLP 構造
//=================================================================================================
word endianW(word in)
{
	union {
		byte B[2];
		word w;
	}	HL, LH;
	HL.w = in;
	LH.B[0] = HL.B[1];
	LH.B[1] = HL.B[0];
	return LH.w;
}

struct BLP_File_Header {
	char  File_ID[4];
	word  WX,WY;			/* Image Size */
	word  Colors;			/* Palette Stat Table Count */
	word  Status;			/* Status & Control bit flags */
			/* _bit0 : skip trace (status)         現在意味なし */
			/* _bit1 : word palette (status)       パレットデータは word (bcp format) */
			/* _bit2 : or put procedure (control)  0を透明として重ね書き(OR書き) */
			/* _bit3 : double mag (control)        2倍に拡大して表示 */
};

struct	blp_dec_block {
	word   *top;		/* image data top */
	int     width;			/* image data width */
	word    wx,wy;		/* image data size */
	byte   *enc;		/* encoded data buffer (pointed to end)*/
	word    max;		/* color max count */
	word    bits;		/* bit flags _bit0 : skip, _bit1 : code */
	byte   *col;
	word    pal_ofset;	/* if pallete code, down offset */
	};

/* blp_dec.c */
int	blp_header_get(struct blp_dec_block *bwp)
{
	byte	*ep = bwp->enc;
	short	c;

	/* 1. ヘッダー情報 (12 bytes) */
	{
		struct BLP_File_Header *bfp = (struct BLP_File_Header *)ep;
		static	char	head[4] = {'B','M','P','C'};

		for (c = 0 ; c < 4 ; c++) {
			if (bfp->File_ID[c] != head[c])  return 0;
		}
		bwp->wx = endianW(bfp->WX);
		bwp->wy = endianW(bfp->WY);
		bwp->max = endianW(bfp->Colors);
		bwp->bits = endianW(bfp->Status);
		ep += sizeof(struct BLP_File_Header);
	}

	/* 2. パレットデータ (Header.Colors * (Header.Status.bit1 ? 2 : 1) bytes) */
	bwp->col = ep;

	/* 3. 圧縮データ */
	bwp->enc = ep + ((bwp->bits & 0x02)==0 ? bwp->max : bwp->max * 2);

	return 1;
}

void  blp_palette_set(struct blp_dec_block *bwp, int Paloffset)
{
	short	c;
	short   Evenmode = bwp->bits & 0x10;
	short	*gpa = (short*)0xe82000;
	gpa += Paloffset;

	if (bwp->bits & 0x02) {
		word  *pp = (word*)bwp->col;
		for (c = 0; c < bwp->max; c++) {
			*gpa++ = *pp++;
			if (Evenmode) gpa++;
		}
	}
}

static	byte	lbuffer[2][1024];

void
  bcp_dec_trace_icore(struct blp_dec_block *bwp, word palette_offset,
					 int add, int mag, int even, int bcp)
/*
 *  bcp format  put tracer
 *    image : word size <- 圧縮データのコードが書き込まれる
 *    palete_offset : 展開パレットコードに加算される (パレットデータもずれていること)
 */
{
	byte  *ep = bwp->enc;  /* encode pointer */
	word  *ip = bwp->top;  /* image  pointer */
	int   width = bwp->width;              /* image width */
	int   ofset = bwp->width - bwp->wx;    /* image line down offset */
	word  *colW = (word*)bwp->col;           /* color table pointer */
	byte  *colB = (byte*)bwp->col;           /* color table pointer */
	word	x,y;
//printf("Color %x,%x,%x,%x\n", colB[0],colB[1],colB[2],colB[3]);
//printf("offset %d\n", ofset);
//bool a = true;
//mag = 0;
	if (mag) ofset *= 2;
	if (mag) width *= 2;

	for (y = bwp->wy ; y != 0 ; y--) {

	    byte *lb1 = lbuffer[ y & 1 ];
	    byte *lb2 = lbuffer[ (y & 1)^1 ];

	    for (x = bwp->wx ; x != 0 ;) {

			/* Alone Pattarn     単一に現われるパターン */
			if (*ep & 0x80) {
				word c, pc;
				if (bcp) {
					c = *ep++ & 0x7f ;
					pc = (even ? c*2 : c) + palette_offset;
				} else {
					c = colB[ *ep++ & 0x7f ];
					pc = c;
				}
				if (!add || (bcp ? endianW(colW[c]) : c)!=0) {
					*ip = pc;
					if (mag) {
						*(ip+1) = pc;
						*(ip+width) = pc;
						*(ip+width+1) = pc;
					}
				}
				ip++; *lb1++ = c; lb2++;
				if (mag) ip++;
				x--;

			/* Continuas Pattarn 連続するパターン */
			} else {
				word c1 = *ep & 0x0f;
				word l = (*ep++ & 0x70) >> 4;
				word pc,cc;

				if (l == 0) {
					l = *ep++ ;
					if (l == 0) {
						l = *ep++ + 256;
					} else if (l == 1) {
						l = *ep++ + 512;
					} else if (l == 2) {
						l = *ep++ + 768;
					} else if (l == 3) {
						l = 1024;
					}
				} else {
					l += 1;
				}

				if (bcp) {
					switch (c1) {
					  case 0 :  cc = *ep++ ;  break;
					  case 13 :  cc = *lb2;  break;
					  case 14 :  cc = *(lb2-1);  break;
					  case 15 :  cc = *(lb2+1);  break;
					  default: cc = c1-1;
					}
					pc = (even ? cc*2 : cc) + palette_offset;

				} else {
					switch (c1) {
					  case 0 :  cc = colB[*ep++];  break;
					  case 13 :  cc = *lb2;  break;
					  case 14 :  cc = *(lb2-1);  break;
					  case 15 :  cc = *(lb2+1);  break;
					  default:  cc = colB[c1-1];
					}
					pc = cc;
				}

				lb2 += l;
				x  -= l;
				if (!add || (bcp ? endianW(colW[c1]) : cc)!=0) {
					while (l != 0) {
						*ip++ = pc;
						if (mag) {
							*ip = pc;
							*(ip+width) = pc;
							*(ip+width-1) = pc;
						}
						*lb1++ = cc;	l--;
						if (mag) ip++;
					}
				} else {
					ip += l;
					if (mag) ip += l;
					while (l != 0) {	*lb1++ = cc;	l--; }
				}

			}
		}
	    ip += ofset;
		if (mag) ip += width;
	}
}



void	bcp_dec_trace(struct blp_dec_block *bwp, word palette_offset)
{
  bcp_dec_trace_icore(bwp, palette_offset,
					 0, 0, 0, 1);
}

void	bcp_add_trace(struct blp_dec_block *bwp, word palette_offset)
{
  bcp_dec_trace_icore(bwp, palette_offset,
					 1, 0, 0, 1);
}

void	bcp_mag_trace(struct blp_dec_block *bwp, word palette_offset)
{
  bcp_dec_trace_icore(bwp, palette_offset,
					 0, 1, 0, 1);
}

void	bcp_magadd_trace(struct blp_dec_block *bwp, word palette_offset)
{
  bcp_dec_trace_icore(bwp, palette_offset,
					 1, 1, 0, 1);
}

void	bcp_even_trace(struct blp_dec_block *bwp, word palette_offset)
{
  bcp_dec_trace_icore(bwp, palette_offset,
					 0, 0, 1, 1);
}

void	blp_dec_trace(struct blp_dec_block *bwp)
{
  bcp_dec_trace_icore(bwp, 0,
					 0, 0, 0, 0);
}


void	blp_dec_core(struct blp_dec_block * bwp)
{

	short  add = ((bwp->bits & 0x04) != 0); /* add mode flag */
	short  mag = ((bwp->bits & 0x08) != 0); /* mag mode flag */
	short  even = ((bwp->bits & 0x10) != 0); /* even mode flag */


	if (bwp->bits & 0x02) {
		if (bwp->bits & 0x10) {
			bcp_even_trace(bwp, bwp->pal_ofset);
		} else {
			if (bwp->bits & 0x04) { /* add mode flag */
				if (bwp->bits & 0x08) { /* mag mode flag */
					bcp_magadd_trace(bwp, bwp->pal_ofset);
				} else {
					bcp_add_trace(bwp, bwp->pal_ofset);
				}
			} else {
				if (bwp->bits & 0x08) { /* mag mode flag */
					bcp_mag_trace(bwp, bwp->pal_ofset);
				} else {
					bcp_dec_trace(bwp, bwp->pal_ofset);
				}
			}
		}
	} else {
		blp_dec_trace(bwp);
	}
}

word Palette[256];

word GRB2RGB(word x6)
{
	int g = (x6 >> 11) & 0x1f;
	int r = (x6 >> 6) & 0x1f;
	int b = (x6 >> 1) & 0x1f;
	return (((r))<<10) | (((g))<<5) | (((b))<<0);
}

void palette_64_setup()
{
	int	i,j,k;
	word	*gpp,*pdp;

	static  int  rt[4] = {0,15,22,31};
	static  int  gt[4] = {0,13,23,31};
	static  int  bt[4] = {0,17,24,31};

	static	unsigned short	upper[64] = {
		0x0001,0x3038,0x2780,0x47B0,0xB000,0xA528,0xD790,0xF7BC,
		0x4604,0x001C,0x0400,0xB79C,0x7000,0x9034,0x7708,0x0001,
		0x2949,0x5AD7,0x0BDB,0x8FB4,0xE7F4,0xCFEA,0xA758,0x764E,
		0x5548,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
		0x984E,0x9896,0x909E,0x88E6,0x892E,0x8176,0x823A,0x7178,
		0x6174,0x516E,0x4168,0x3122,0x211C,0x1116,0x0112,0x00CC,
		0x2D00,0x4640,0x5EC0,0x66C2,0x76C4,0x8708,0x970C,0xA710,
		0xAF52,0xBF56,0xCF58,0xDF9C,0xEFA0,0xFFE4,0xFF75,0xFFFF,
		};

	gpp = Palette;
	for (i=0;i<4;i++) {
		for (j=0;j<4;j++) {
			for (k=0;k<4;k++) {
				int c = (gt[i]<<11)|(rt[j]<<6)|(bt[k]<<1);
				*gpp++ = GRB2RGB(c);
				*gpp++ = GRB2RGB(c);
			}
		}
	}

	pdp = upper;
	for (i=0;i<64;i++) {			/* pallet_set(); */
		*gpp++ = GRB2RGB(*pdp);
		*gpp++ = GRB2RGB(*pdp);
		pdp++;
	}
}

void palette_code_setup(int max, word* pal)
{
	word* gpp = Palette;
	for(int i=0 ; i<max ; i++ ){
		*gpp++ = GRB2RGB(endianW(pal[i]));
	}
}

void palette_bw_setup()
{
	int  i;
	word	*gpp;
    static word	pal[128] = { 
	/*	マップ用ローカルパレット 64色(DUMMY)	*/
		0x0000,0x0022,0x002E,0x003C,0x03C0,0x03E2,0x03EE,0x03FC,
    	0x0580,0x05A2,0x05AE,0x05BC,0x0780,0x07A2,0x07AE,0x07BC,
    	0x6000,0x6022,0x782E,0x783C,0x63C0,0x73E2,0x7BEE,0x843C,
    	0x7D80,0x7DA2,0x75AE,0x7DBC,0x7780,0x7FA2,0x7FAE,0x7FBC,
    	0xB000,0xA022,0xA02E,0xA03C,0xB440,0xB462,0xA3EE,0xA3FC,
    	0xA580,0xADA2,0xA5AE,0xBDBC,0x9744,0x9F5C,0xA7AE,0xAFBC,
    	0xF000,0xF022,0xF02E,0xF03C,0xF3C0,0xF3E2,0xF3EE,0xF3FC,
    	0xF580,0xF5A2,0xF5AE,0xF5BC,0xD78E,0xE79C,0xF7AE,0xF7BC,
	/*	標準 16色	*/
    	0x0001,0x6565,0x6784,0x47B0,0xB000,0xA528,0xD790,0xF7BC,
    	0x4604,0x001C,0x0400,0xB79C,0x7000,0x9034,0x7708,0x08d7,
	/*	フレーム 16色	*/
		0x08d7,0x0098,0xA334,0xF7BC,0x712C,0x5124,0x38A6,0x6804,
		0xB700,0x8700,0x2640,0x3408,0x7320,0xC140,0x7506,0xAEDC,
	/*	ミニビジュアル＆顔 32色	*/
		0x0000,0xC72B,0x85DD,0x6495,0x8BAD,0xCDB9,0xA529,0x2CC1,
		0x3ACB,0xAE97,0x95A1,0x0943,0x3953,0x4999,0x1807,0xE739,
		0xDEF1,0xA6E1,0x7D29,0xB75F,0x3141,0x7381,0x0001,0x5295,
		0xCE73,0x7393,0xA799,0x7B27,0xCE5D,0x629D,0x8421,0x39D1 };
	/*	80番パレットはアイコンの下地色に予約	*/

	gpp = Palette;
	for( i=0 ; i<128 ; i++ ){
		*gpp++ = GRB2RGB(pal[i]);
		*gpp++ = GRB2RGB(pal[i]);
	}
}



word*  blp_mem_dec(byte *ebuf, int *_wx, int *_wy, int pal_ofset)
{
	struct blp_dec_block bvwork, *bwp = &bvwork;

	bwp->top = NULL;
	bwp->width = 0;
	bwp->enc = ebuf;
	bwp->pal_ofset = pal_ofset;

	if (!blp_header_get(bwp)) return NULL;
	if (bwp->width == 0)	bwp->width = bwp->wx;

	std::cout << "Size " << bwp->wx << "x" << bwp->wy << std::endl;
	std::cout << "color " << bwp->max << std::endl;
	std::cout << "Status " << bwp->bits << std::endl;

	//palette_64_setup();
	if (bwp->bits & 0x02) {
		palette_code_setup(bwp->max, (word*)bwp->col);
	} else {
		palette_bw_setup();
	}

		int wx = bwp->wx;
		int wy = bwp->wy;
		if (bwp->bits & 0x08) {
			wx *= 2;
			wy *= 2;
		}
		word *bmp = new word[ wx * wy ];
		bwp->top = bmp;
		blp_dec_core(bwp);

#if 0
	for (int c = 0; c < bwp->wx * bwp->wy; c++) {
		bwp->top[c] = Palette[bwp->top[c]];
	}

	*_wx = bwp->wx;
	*_wy = bwp->wy;

	return bwp->top;
#endif

		word wwx = wx*3/2;
		word wwy = wy;
		word *bmpw = new word[ wwx * wwy ];
		for (int y = 0; y < wwy; y++) {
			for (int x = 0; x < wx/2; x++) {
				word p0 = Palette[bmp[y*wx+x*2+0]];
				word p1 = Palette[bmp[y*wx+x*2+1]];
				word r0 = (p0 >> 10) & 31;
				word g0 = (p0 >> 5) & 31;
				word b0 = (p0 >> 0) & 31;
				word r1 = (p1 >> 10) & 31;
				word g1 = (p1 >> 5) & 31;
				word b1 = (p1 >> 0) & 31;
				word r2 = (r0+r1)>>1;
				word g2 = (g0+g1)>>1;
				word b2 = (b0+b1)>>1;
				word p2 = (((r2))<<10) | (((g2))<<5) | (((b2))<<0);
				
				bmpw[y*wwx+x*3+0] = p0;
				bmpw[y*wwx+x*3+1] = p2;
				bmpw[y*wwx+x*3+2] = p1;
			}
		}

	*_wx = wwx;
	*_wy = wwy;

	free(bmp);
	return bmpw;
}


//=================================================================================================
// Section イメージデータ BMP 出力
//=================================================================================================
/* wtypes.h */
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef int INT;
typedef int BOOL;
typedef int LONG;
typedef UINT WPARAM;
typedef unsigned int DWORD;

/* wingdi.h */
typedef struct tagBITMAPFILEHEADER {
        WORD    bfType;					/* BM */
        DWORD   bfSize;					/* ファイルサイズ */
        WORD    bfReserved1;
        WORD    bfReserved2;
        DWORD   bfOffBits;				/* オフセット */
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER{
        DWORD      biSize;				/* ヘッダサイズ */
        LONG       biWidth;				/* 画像の幅 */
        LONG       biHeight;			/* 画像の高さ */
        WORD       biPlanes;			/* プレーン数 */
        WORD       biBitCount;			/* カラービット数 */
        DWORD      biCompression;		/* 圧縮方式 */
        DWORD      biSizeImage;			/* 画像サイズ */
        LONG       biXPelsPerMeter;		/* 解像度(横) */
        LONG       biYPelsPerMeter;		/* 解像度(縦) */
        DWORD      biClrUsed;			/* 使用している色数 */
        DWORD      biClrImportant;		/* 重要な色数 */
} BITMAPINFOHEADER;

BITMAPFILEHEADER	bmpHdr;
BITMAPINFOHEADER	bmpInfo;

#define  _Image_Buffer_Size		(_W_PRJ_SCREEN_H*_W_PRJ_SCREEN_W)

void  bmp_save( const char *name, int wx, int wy, word* screen)
{
	FILE	*fp;
	char	fn[64], *cp;

	strcpy( fn, name );
	if ( (cp = strchr( fn, '.' )) == NULL ) {
		strcat( fn, ".BMP" );
	} else {
		strcpy( cp, ".BMP" );
	}
	printf( "Saving %s\n", fn );

	if( !(fp=fopen(fn,"wb")) ) {
		fprintf(stderr,"File can't open\n");
		exit(1);
	}

	{
		long  size;
		int   h;

		/* BMP ヘッダー */
		((char *) &bmpHdr.bfType)[0] = 'B';
		((char *) &bmpHdr.bfType)[1] = 'M';
		size = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+ (2*wx*wy);
		bmpHdr.bfSize = size;				/* ファイルサイズ */
		bmpHdr.bfReserved1 = 0;
		bmpHdr.bfReserved2 = 0;
		size = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
		bmpHdr.bfOffBits = size;				/* オフセット */
		fwrite( &bmpHdr, sizeof(BITMAPFILEHEADER), 1, fp );

		/* BMP インフォメーション */
		bmpInfo.biSize = sizeof(BITMAPINFOHEADER);				/* ヘッダサイズ */
		bmpInfo.biWidth = wx;				/* 画像の幅 */
		bmpInfo.biHeight = wy;			/* 画像の高さ */
		bmpInfo.biPlanes = 1;			/* プレーン数 */
		bmpInfo.biBitCount = 16	/*24*/;			/* カラービット数 */
		bmpInfo.biCompression = 0;		/* 圧縮方式 */
		bmpInfo.biSizeImage = 0;			/* 画像サイズ */
		bmpInfo.biXPelsPerMeter = 0;		/* 解像度(横) */
		bmpInfo.biYPelsPerMeter = 0;		/* 解像度(縦) */
		bmpInfo.biClrUsed = 0;			/* 使用している色数 */
		bmpInfo.biClrImportant = 0;		/* 重要な色数 */
		fwrite( &bmpInfo, sizeof(BITMAPINFOHEADER), 1, fp );

		/* イメージ転送 */
		for ( h = 0 ; h < wy ; h++ ) {
			fwrite( screen + (wy-1-h)*(wx), sizeof(word), wx, fp );
		}
	}
	fclose(fp);
}


//=================================================================================================
// Section BLP ローダ
//=================================================================================================
static	unsigned char	dbuf[128000];

class BlpLoader
{
	const std::string rootpath;
	const std::string outpath;
	bool        verbose;

	std::ostream* _ofp;

public:
	BlpLoader(ArgOptions& options)
		: rootpath( options["-p"] )
		, outpath( options["-o"] )
		, verbose( options["-v"].Check() )
		, _ofp(NULL)
	{
	}
	~BlpLoader() {
		if (_ofp != NULL) delete(_ofp);
	}

	void ScanFile(const std::string filepath)
	{
		// 出力ファイル
		std::string outfile = filepath;
		outfile = std::regex_replace( outfile, std::regex("^.*/"), "");
		outfile = std::regex_replace( outfile, std::regex("\\..*$"), "");
		outfile += "b.bmp";
		if (std::regex_match( outpath, std::regex(".*/$"))) {
			outfile = outpath + outfile;
		}
		if (verbose) {
			std::cout << ("Out File "+outfile) << std::endl;
		}

		//  ファイルを開く
		std::string srcfile = rootpath+filepath;
		if (verbose) {
			std::cout << ("Src File "+srcfile) << std::endl;
		}


		//
		{
			FILE	*fp;
			if ((fp = fopen(srcfile.c_str(), "rb"))) {
				unsigned char *dbp = dbuf;
				while (fread(dbp, 1, 65536, fp) == 65536)	dbp += 65536;
				fclose(fp);
			
			} else {
				exit(0);
			}
		}
		int wx, wy;
		word *image = blp_mem_dec(dbuf, &wx, &wy, 0);

		bmp_save( outfile.c_str(), wx, wy, image);

		free(image);
	}
};

//=================================================================================================
// Section メインエントリー
//=================================================================================================
int main(int argc, char* argv[])
{
	if (argc < 2) {
		std::cout << "blp2bmp [options] blp-file" << std::endl;
		std::cout << " BLP BMP 変換 ツール" << std::endl;
		std::cout << " options" << std::endl;
		std::cout << "  -o path : 出力パス指定 result out path (ex. -o ../ )" << std::endl;
		std::cout << "  -p path : ソースパス指定 script root path ( ex. -p ../script/ )" << std::endl;
		std::cout << "  -v      : Verbose" << std::endl;
		return 1;
	}

	std::vector<std::string> filelist;
	ArgOptions options;
	options["-v"] = false;  // Verbose
	options["-a"] = false;  // Append
	options["-p"] = "";  // Load Path
	options["-o"] = "";  // Out Path

	ArgAnalyzer(argc, argv, filelist, options);

	//std::cout << "BITMAPFILEHEADER " << sizeof(BITMAPFILEHEADER) << std::endl;
	//std::cout << "BITMAPINFOHEADER " << sizeof(BITMAPINFOHEADER) << std::endl;

	//if (options["-r"].Check()) {
	//}
	BlpLoader blploader(options);
	for (std::vector<std::string>::iterator itr = filelist.begin(); itr != filelist.end(); ++itr) {
		std::string filepath = (*itr);
		blploader.ScanFile(filepath);
	}


    return 0;
}
