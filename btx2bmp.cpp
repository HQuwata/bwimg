//=================================================================================================
// ファイル名:	btx2bmp.cpp
// 主題:		btx を bmp に変換
// 履歴:		2016.01.13	H.Kuwata	新規作成
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


#ifdef Compress_Format

圧縮方法
	テキストプレーンの縦方向ランレングス圧縮
	統計頻度でソートしたパターン変換テーブルを持つ
	ランレングスは連続するパターンの差分(テーブルにより参照)の個数を現わす
		55 55 55 55   ->  00 x 4
		aa 55 aa 55   ->  FF x 4

ファイルフォーマット

	1. ヘッダー情報 (16 bytes)
	struct Header {
		char  File_ID[4] = 'BTXC';
		word  Status;			/* Status & Control bit flags */
			/* _bit0 : no palette (status)         パレットデータはなし */
			/* _bit8 : or put procedure (control)  0を透明として重ね書き(OR書き) */
		short Block_Count;		/* Total Pattarn-Block 数 */
		byte  reserve[8];
	}

	2. パレットデータ (16 words ただし Header.Status.bit0 が 0 の場合のみ)
	word  Palette[16];

	3. ブロックデータテーブル (16 x Header.Block_Count)
	struct Block_Table {
		word  Run_Offset;		/* Run bit start offset address (Top+T0_Offset) */
		word  Data_Offset;		/* Data start offset address (Top+T1_Offset) */
		word  reserve1;
		byte  delta1,delta2;	/* Animation Delta Cut No */
		word  Width_X, Width_Y;	/* Image Size */
		word  Home_X,  Home_Y;	/* Image Put Home Offset Position */
								/* 描画 Home ポジションの相対補正値 */
	}

	4. ブロックプレーンデータ (variable x 4 x Header.Block_Count)
	word  Pattarn_Count;		/* パターンテーブル Count 数 (max 28) */
	byte  Pattarn_Table[Pattarn_Count];	/* パターンテーブル */
	byte  Press_Data[];			/* 圧縮データ */

圧縮データフォーマット

	Alone Pattarn     単一に現われるパターン (Run bit == 0)
		pppppppp
			p : 0-255  pattarn

	Continuas Pattarn 連続するパターン
		dllppppp [d] [p] [l1] [l2-256]
			d : 0    dup-diff. = 0x00
			    1,   dup-diff. = 次の 1byte{1-255};

			p : 1-28  pattarn = Pattarn_Table[p-1];
			    0,   pattarn = 次の 1byte{0-255};
			    29,   pattarn = 展開画面の左上と同じ
			    30,   pattarn = 展開画面の左と同じ
			    31,   pattarn = 展開画面の左下と同じ

			l : 1-3   length = l + 1; {2-4}
			    0,    length = 次の 1byte{5-255};
			            if 次の 1byte == 0; length = その次の 1byte + 256; {256-511}{512}

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
// Section メモリパレット
//=================================================================================================
word Palette[256];

word GRB2RGB(word x6)
{
	if (x6 == 0) return 0;
	if (x6 == 1) return 1;

	int g = (x6 >> 11) & 0x1f;
	int r = (x6 >> 6) & 0x1f;
	int b = (x6 >> 1) & 0x1f;
	return (((r))<<10) | (((g))<<5) | (((b))<<0);
}

void palette_code_setup(word* pal)
{
	word* gpp = Palette;
	for(int i=0 ; i<16 ; i++ ){
		//printf("p%d:%x\n",i,endianW(pal[i]));
		*gpp++ = GRB2RGB(endianW(pal[i]));
	}
}

void palette_bw_setup()
{
	int  i;
	word	*gpp;
    static word	text_pal[16] = {	0x0000,0xbdef,0x9973,0x796b,
									0x5967,0x3967,0x08d7,0xffff,
									0x0d43,0x086b,0x8cc3,0x68cb,
									0x9a4b,0x1b43,0x5d43,0x0001 };

	gpp = Palette;
	for( i=0 ; i<16 ; i++ ){
		*gpp++ = GRB2RGB(text_pal[i]);
	}
}

//=================================================================================================
// Section BTX 構造
//=================================================================================================
struct BTX_File_Header {
	char  File_ID[4];
	word  Status;			/* Status & Control bit flags */
			/* _bit0 : no palette (status)         パレットデータはなし */
			/* _bit8 : or put procedure (control)  0を透明として重ね書き(OR書き) */
	word Block_Count;		/* Total Pattarn-Block 数 */
	byte  reserve[8];
};

struct BTX_Block_Table {
	word  Run_Offset;		/* Run Bit start offset address (Top+T0_Offset) */
	word  Data_Offset;		/* Data start offset address (Top+T1_Offset) */
	word  res1;
	byte  Delta1,Delta2;	/* Animation Delta Pair No. */
	word  Width_X, Width_Y;	/* Image Size */
	word  Home_X,  Home_Y;	/* Image Put Home Offset Position */

	word WidthX() { return endianW(Width_X); }
	word WidthY() { return endianW(Width_Y); }
	word HomeX() { return endianW(Home_X); }
	word HomeY() { return endianW(Home_Y); }
};

typedef  struct	BTX_dec_header {
	word	max;		/* max pattarn block bumber : init=0 > return max */
	word	bits;		/* bit flags */
	word	*palette;	/* palette entry */
	struct BTX_Block_Table *block;	/* block data top */
}		BTX_Dec_Header;

/* btx_dec.c */
#define  _alone  0
#define  _upper  29
#define  _left   30
#define  _lower  31

void
  btx_header_get(BTX_Dec_Header * bhp, word * decode)
{
	word	*dp = decode;
	{
		struct BTX_File_Header *bfp = (struct BTX_File_Header *)dp;

		bhp->max = endianW(bfp->Block_Count);
		bhp->bits = endianW(bfp->Status);
		dp += sizeof(struct BTX_File_Header) / sizeof(word);
	}

	/* palette load */
	if( (bhp->bits & 0x0001)==0 ) {
		bhp->palette = (word*)dp;
		dp += 16;

	} else {
		bhp->palette = 0;
	}

	/* block header load */
	bhp->block = (struct BTX_Block_Table *)dp;
}



void
  btx_palette_set(BTX_Dec_Header *bhp)
{
	/*
	word	*tp = (word*)0xe82200;
	word	*ep = bhp->palette ;
	short	c;
	for( c = 16 ; c > 0 ; c-- ) {
		*tp++ = *ep++;
	}
	*/
	if (bhp->palette) {
		palette_code_setup(bhp->palette);
	}
}

byte  *
  btx_dec_trace(struct BTX_Block_Table * bwp, word * decode, byte * mbuffer,
			   int width, int plane)
{
	int	p,q,r;

	word  *run = (word*)((long)decode + (long)endianW(bwp->Run_Offset));
	word  *block = (word*)((long)decode + (long)endianW(bwp->Data_Offset));
	byte  *pattarn = (byte*)(block + 1);
	byte  *data = pattarn + endianW(*block);

	word   mask = 0x8000;

	for (p = 4; p > 0; p--) {
		byte *line = mbuffer;
		for (r = bwp->WidthX(); r > 0 ; r--) {
			byte *lp = line++;
			byte *bp = lp - 1;
#ifdef  DECDEBUG
printf("--- column %d\n",bwp->WidthX()-r);
#endif
			for (q = bwp->WidthY(); q > 0; ) {
				byte  d = *data++;

				/* run length pattarn */
				if (endianW(*run) & mask) {
					byte  diff, ptrn;
					word   len;

					/* diff pattarn */
					if ((d & 0x80)==0) {
						diff = 0;
					} else {
						diff = *data++;
					}

					/* put pattarn */
					{
						byte dd = d & 0x1f;
						if (dd == 0) {
							ptrn = *data++;
						} else if (dd == _left) {
							ptrn = *(bp);
						} else if (dd == _upper) {
							ptrn = *(bp - width);
						} else if (dd == _lower) {
							ptrn = *(bp + width);
						} else {
							ptrn = pattarn[dd-1];
						}
					}

					/* length */
					if ((len = (d & 0x60) >> 5) == 0) {
						if ((len = *data++) == 0) {
							len = *data++ + 256;
						} else if (len == 1) {
							len = *data++ + 512;
						}
					} else {
						len += 1;
					}

#ifdef  DECDEBUG
  printf("%2.2X(%2.2X)..%d\n",ptrn, diff, len);
#endif
					q -= len;
					if (diff == 0) {
						while (len > 0) {
							*lp = ptrn;
							lp += width;
							bp += width;
							len--;
						}

					} else {
						while (len > 0) {
							*lp = ptrn;
							lp += width;
							bp += width;
							ptrn ^= diff;
							len--;
						}
					}
						
				/* alone pattarn */
				} else {
#ifdef  DECDEBUG
  printf("%2.2X..1\n",d);
#endif
					*lp = d;
					lp += width;
					bp += width;
					q--;
				}

				/* mask next */
				if( (mask >>= 1) == 0 ) {
					run++;
					mask = 0x8000;
				}
			}
		}
		mbuffer += plane;
	}

	return mbuffer;
}

byte  *
  btx_mem_dec(struct BTX_Block_Table * bwp, word * decode, byte * mbuffer)
{
	return btx_dec_trace(bwp, decode, mbuffer, bwp->WidthX(), bwp->WidthX() * bwp->WidthY());
}

void  
  btx_dec_put(struct BTX_Block_Table * bwp, word * decode, long home)
{
	home += bwp->HomeY() * 0x80 + bwp->HomeX();
	btx_dec_trace(bwp, decode, (byte*)home, 0x80, 0x20000);
}

void
  btx_decode(BTX_Dec_Header * bhp, word * decode, int hx, int hy)
{
	struct BTX_Block_Table *bwp;
	short	c;

	btx_header_get(bhp, decode);
	btx_palette_set(bhp);

	bwp = bhp->block;
	for( c = 0 ; c < bhp->max ; c++ ) {
		/*btx_block_put( bwp , decode , 0xe00000 + hy * 0x80 + hx * 2 );*/
		btx_dec_put( bwp , decode , 0xe00000 + hy * 0x80 + hx * 2 );
		bwp++;
	}
}

void
btx_mem_put(struct BTX_Block_Table * bwp, byte * mbuffer, long home)
/*
 *  caution word position only
 */
{
	long  msize = (bwp->WidthX() * (bwp->WidthY()-1))/sizeof(word);
	long  psize = (0x20000 - bwp->WidthX())/sizeof(word);
	short x,y,p;

	home += bwp->HomeY() * 0x80 + bwp->HomeX();

	for (y = bwp->WidthY(); y != 0; y--) {
		word *ip = (word*)home;
		word *mp = (word*)mbuffer;
		for (p = 4; p != 0; p--) {
			for (x = bwp->WidthX()/sizeof(word); x != 0; x--) {
				*ip++ = *mp++;
			}
			ip += psize;
			mp += msize;
		}
		home += 0x80;
		mbuffer += bwp->WidthX();
	}
}

void
btx_mem_bmp(struct BTX_Block_Table * bwp, byte * mbuffer, word *bmp)
{
	long  msize = (bwp->WidthX() * (bwp->WidthY()));
	byte *m0 = mbuffer;
	byte *m1 = mbuffer + msize;
	byte *m2 = mbuffer + msize*2;
	byte *m3 = mbuffer + msize*3;
	word *ip = bmp;
	for (int y = bwp->WidthY(); y != 0; y--) {
		for (int x = bwp->WidthX(); x != 0; x--) {
			byte d0 = *m0++; 
			byte d1 = *m1++; 
			byte d2 = *m2++; 
			byte d3 = *m3++; 
			int mask = 0x80;
			for (int mask = 0x80; mask != 0; mask>>=1) {
				word pd = 0;
				pd |= ((d0 & mask) != 0 ? 1 : 0);
				pd |= ((d1 & mask) != 0 ? 2 : 0);
				pd |= ((d2 & mask) != 0 ? 4 : 0);
				pd |= ((d3 & mask) != 0 ? 8 : 0);
				*ip++ = pd;
			}
		}
	}
}

void
btx_mem_xor(struct BTX_Block_Table * bwp, byte * mbuffer, long home)
/*
 *  caution word position only
 */
{
	long  msize = bwp->WidthX() * bwp->WidthY();
	short x,y,p;

	home += bwp->HomeY() * 0x80 + bwp->HomeX();

	for (y = bwp->WidthY(); y != 0; y--) {
		long *p0 = (long*)home;
		long *p1 = (long*)(home+0x20000);
		long *p2 = (long*)(home+0x40000);
		long *p3 = (long*)(home+0x60000);
		long *mp = (long*)mbuffer;
		for (x = bwp->WidthX()/sizeof(long); x != 0; x--) {
			long mo = msize;
			*p1++ ^= *((long*)((char*)mp+mo));	mo += msize;
			*p2++ ^= *((long*)((char*)mp+mo));	mo += msize;
			*p3++ ^= *((long*)((char*)mp+mo));
			*p0++ ^= *mp++;
		}
		if (bwp->WidthX() & 0x02) {
			long mo = msize;
			*((word*)p1) ^= *((word*)((char*)mp+mo));	mo += msize;
			*((word*)p2) ^= *((word*)((char*)mp+mo));	mo += msize;
			*((word*)p3) ^= *((word*)((char*)mp+mo));
			*((word*)p0) ^= *((word*)mp);
		}

		home += 0x80;
		mbuffer += bwp->WidthX();
	}
}

//=================================================================================================
// Section BTX ローダ
//=================================================================================================
static	unsigned short	dbuf[65536];
static	unsigned char	image[512*512/8*4];

class BtxLoader
{
	const std::string rootpath;
	const std::string outpath;
	bool        verbose;

	std::ostream* _ofp;

public:
	BtxLoader(ArgOptions& options)
		: rootpath( options["-p"] )
		, outpath( options["-o"] )
		, verbose( options["-v"].Check() )
		, _ofp(NULL)
	{
	}
	~BtxLoader() {
		if (_ofp != NULL) delete(_ofp);
	}

	void ScanFile(const std::string filepath)
	{
		// 出力ファイル
		std::string outfile = filepath;
		outfile = std::regex_replace( outfile, std::regex("^.*/"), "");
		outfile = std::regex_replace( outfile, std::regex("\\..*$"), "");
		outfile += "v.bmp";
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


		/* 画像ファイルの読み込み */
		{
			FILE	*fp;
			if ((fp = fopen(srcfile.c_str(), "rb"))) {
				unsigned char *dbp = (unsigned char *)dbuf;
				while (fread(dbp, 1, 65536, fp) == 65536)	dbp += 65536;
				fclose(fp);
			
			} else {
				exit(0);
			}
		}

		/* 画像ファイルの展開と表示 */
		BTX_Dec_Header dechead, *dec = &dechead;
		btx_header_get(dec, dbuf);
		btx_palette_set(dec);

		std::cout << "Blocks " << dec->max << std::endl;
		std::cout << "Status " << dec->bits << std::endl;

		BTX_Block_Table * bwp = dec->block;
		btx_mem_dec( bwp , dbuf , image );

		std::cout << "Size " << bwp->WidthX() << "x" << bwp->WidthY() << std::endl;
		int wx = bwp->WidthX() * 8;
		int wy = bwp->WidthY();
	
		word *bmp = new word[ wx * wy ];
		btx_mem_bmp( bwp , image , bmp );
		//for (int c = 0; c < wx * wy; c++) {
		//	bmp[c] = Palette[bmp[c]];
		//}

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

		bmp_save( outfile.c_str(), wwx, wwy, bmpw);

		free(bmp);
	}
};

//=================================================================================================
// Section メインエントリー
//=================================================================================================
int main(int argc, char* argv[])
{
	if (argc < 2) {
		std::cout << "btx2bmp [options] btx-file" << std::endl;
		std::cout << " BTX BMP 変換 ツール" << std::endl;
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
	BtxLoader btxloader(options);
	for (std::vector<std::string>::iterator itr = filelist.begin(); itr != filelist.end(); ++itr) {
		std::string filepath = (*itr);
		btxloader.ScanFile(filepath);
	}


    return 0;
}
