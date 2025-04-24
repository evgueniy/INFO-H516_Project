#ifndef ICSP_CODEC_ENCODER
#define ICSP_CODEC_ENCODER

#include <iostream>
#include <iomanip>
#include <math.h>
#include <stdlib.h>
#include <memory.h>
#include <time.h>
#include <limits.h>

using namespace std;

#ifdef WIN_MODE
#include <windows.h>
#endif

#define ALL_INTRA 0
#define INTRA 1
#define INTER 2
#define CB 3
#define CR 4
#define SAVE_Y 5
#define SAVE_YUV 6 

typedef enum
{
	I_FRAME=0,
	P_FRAME=1,
	B_FRAME=2
}E_FRAME_TYPE;

typedef enum
{
	SUCCESS=0,
	UNENOUGH_PARAM,
	UNCORRECT_PARAM,
	FAIL_MEM_ALLOC
}E_ERROR_TYPE;

typedef struct 
{
	char yuv_fname[256];
	int total_frames;
	int QP_DC;
	int QP_AC;
	int intra_period;
	int multi_thread_mode;
	int nthreads;
}cmd_options_t;

struct Block8d { double block[8][8]; };
struct Block8i { int block[8][8]; };
struct Block8u { unsigned char block[8][8];};
struct Block8s { signed char block[8][8]; };
struct Block8f { float block[8][8]; };

struct Block16d { double block[16][16]; };
struct Block16i { int block[16][16]; };
struct Block16u { unsigned char block[16][16]; };
struct Block16s { signed char block[16][16]; };
struct Block16f { float block[16][16]; };
struct MotionVector { int x,y;};

typedef struct
{
	int nframe;
	int width;
	int height;

	unsigned char* Ys;
	unsigned char* Cbs;
	unsigned char* Crs;	
}YCbCr_t;

typedef struct 
{
	// Big block size, 16x16
	int blocksize1;
	// Small block size, 8x8
	int blocksize2;
	// Original Y values, in blocks of size `blocksize1`
	Block16u *originalblck16;
	// Original Y values, in blocks of size `blocksize2`
	Block8u **originalblck8;
	Block8i **intraErrblck;
	int DPCMmodePred[4];
	int IDPCMmode[4];
	int MPMFlag[4];
	int intraPredMode[4];
	Block8d **intraDCTblck;
	Block8d **intraInverseDCTblck;
	Block8i **intraQuanblck;
	int intraACflag[4];
	Block8i **intraInverseQuanblck;
	Block16u intraRestructedblck16;
	Block8u **intraRestructedblck8;
	int *intraReorderedblck8[4];

	MotionVector mv;
	MotionVector Reconstructedmv;
	Block16i *interErrblck16;
	Block8i **interErrblck8;
	Block8d **interDCTblck;
	Block8d **interInverseDCTblck;
	Block8i **interQuanblck;
	int interACflag[4];
	Block8i **interInverseQuanblck;
	Block16i *interInverseErrblck16;
	int *interReorderedblck8[4];

	int numOfBlock16;
}BlockData;

typedef struct
{
	int blocksize;
	Block8u *originalblck8;
	Block8d *intraDCTblck;
	Block8i *intraQuanblck;
	int intraACflag;
	Block8d *intraInverseDCTblck;
	Block8i *intraInverseQuanblck;
	int *intraReorderedblck;

	MotionVector mv;
	Block8i *interErrblck;
	Block8d *interDCTblck;
	Block8i *interQuanblck;
	int interACflag;
	Block8d *interInverseDCTblck;
	Block8i *interInverseQuanblck;
	int *interReorderedblck;
}CBlockData;

typedef struct
{
	// Raw Y values
	unsigned char *Y;
	// Raw U values
	unsigned char *Cb;
	// Raw V values
	unsigned char *Cr;
	// Y values in blocks of 16x16 and 8x8
	BlockData *blocks;
	// U values in blocks of 8x8
	CBlockData *Cbblocks;
	// V values in blocks of 8x8
	CBlockData *Crblocks;

	// Total number of 16x16 blocks
	int nblocks16;
	// Number of 8x8 blocks in one 16x16 block (despite the var name, see initialization in `splitBlocks`)
	int nblocks8;
	// Number of 16x16 blocks in width
	int splitWidth;
	// Number of 16x16 blocks in height
	int splitHeight;
	int numOfFrame;
		
	// Number of UV values in width
	int CbCrWidth;
	// Number of UV values in height
	int CbCrHeight;
	// Number of UV blocks in width
	int CbCrSplitWidth;
	// Number of UV blocks in height
	int CbCrSplitHeight;
	// Total UV values (despite the var name, see initialization in `splitBlocks`)
	int totalcbcrblck;

	unsigned char *reconstructedY;	// ���� �������� �����ϰų� ����ϰ� ���� ������ �����Ǿ��� �������� free �ص��� ��
	unsigned char *reconstructedCb;
	unsigned char *reconstructedCr;

}FrameData;


typedef struct
{
	int blocksize1;
	int blocksize2;
	int IDPCMmode[4];
	int MPMFlag[4];
	int intraPredMode[4];
	Block8i **intraInverseQuanblck;
	Block8d **intraInverseDCTblck;
	Block16i *intraInverseErrblck16;
	Block16u *intraRestructedblck16; // encoding �Ҷ��� �����Ͱ� �ƴ�
	Block8u **intraRestructedblck8;

	MotionVector Reconstructedmv;
	Block8i **interInverseQuanblck;
	Block8d **interInverseDCTblck;
	Block16i *interInverseErrblck16;
}DBlockData;


typedef struct
{
	DBlockData *blocks;
	
	unsigned char* decodedY;
	unsigned char* decodedCb;
	unsigned char* decodedCr;
}DFrameData;

const double pi = 3.14159265359;
const float costable[8][8] =
	{1.0,        1.0,        1.0,       1.0,       1.0,       1.0,       1.0,       1.0,
	 0.980785,   0.83147,    0.55557,   0.19509,  -0.19509,  -0.55557,  -0.83147,  -0.980785,
	 0.92388,    0.382683,  -0.382683, -0.92388,  -0.92388,  -0.382683,  0.382683,  0.92388,
	 0.83147,   -0.19509,   -0.980785, -0.55557,   0.55557,   0.980785,  0.19509,  -0.83147,
	 0.707107,  -0.707107,  -0.707107,  0.707107,  0.707107, -0.707107, -0.707107,  0.707107,
	 0.55557,   -0.980785,   0.19509,   0.83147,  -0.83147,  -0.19509,   0.980785, -0.55557,
	 0.382683,  -0.92388,    0.92388,  -0.382683, -0.382683,  0.92388,  -0.92388,   0.382683,
	 0.19509,   -0.55557,    0.83147,  -0.980785,  0.980785, -0.83147,   0.55557,  -0.19509};	
const double irt2 = 1.0/sqrt(2.0); // inverse square root 2

#pragma pack(push, 1)	
struct header
{
	char intro[5];
	unsigned short height;
	unsigned short width;
	unsigned char QP_DC;
	unsigned char QP_AC;
	unsigned char DPCMmode;
	unsigned short outro;	
};
#pragma pack(pop)

struct Statistics {
	// Assuming all videos are of 300 frames
	static constexpr auto frameCount = 300;
	double   psnr[frameCount+1];
	unsigned totalAcBits[frameCount];
	unsigned totalDcBits[frameCount];
	unsigned totalMvBits[frameCount];
	unsigned totalEntropyBits[frameCount];

	// Values range from 2 to 22
	unsigned dcNbitsHistogram[32];
	unsigned acNbitsHistogram[32];
	unsigned mvxNbitsHistogram[32];
	unsigned mvyNbitsHistogram[32];
};

class IcspCodec
{
public:
	YCbCr_t YCbCr;
	FrameData *frames; // ������ �迭; �� �������� �迭�� �ϳ��� ����; ����ü �迭
	int QstepDC;	// only 1 or 8 or 16;
	int QstepAC;	// only 1 or 16;

public:
	void init(int nframe, char* imgfname, int width, int height, int QstepDC, int QstepAC);
	void encoding(cmd_options_t* opt, Statistics *stats = nullptr);
	~IcspCodec(); 
};
/*Data collection functions*/
void computePsnr(FrameData* frames,const int nframes,const int width, const int height ,Statistics *stats);
void writeFrameStats(const Statistics &stats, char* fname, int intra_period, int QstepDC, int QstepAC);
void writeHistogramStats(const Statistics &stats, char* fname, int intra_period, int QstepDC, int QstepAC);
/* parsing command function */
void set_command_options(int argc, char *argv[], cmd_options_t* cmd);

/* message function */
void print_frame_end_message(int curr_frame_num, int frame_type);
void print_error_message(int err_type, char* func_name);
void print_help_message();

// multi-thread functions
void multi_thread_encoding(cmd_options_t* opt, FrameData* frames);
void *encoding_thread(void* arg);

// single-thread functions
void single_thread_encoding(FrameData* frames, YCbCr_t* YCbCr,char* fname, int intra_period, int QstepDC, int QstepAC, Statistics *stats = nullptr);

/* initiation function */
int YCbCrLoad(IcspCodec &icC, char* fname, const int nframe, const int width, const int height);
int splitFrames(IcspCodec &icC);
int splitBlocks(IcspCodec &IcC, int blocksize1, int blocksize2);

/* intra prediction function */
void intraPrediction(FrameData& frm, int QstepDC, int QstepAC);
int allintraPrediction(FrameData* frames, int nframes, int QstepDC, int QstepAC);
void intraImgReconstruct(FrameData &frm);
void DPCM_pix_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth);
void IDPCM_pix_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth);
void intraCbCr(FrameData& frm, CBlockData &cbbd, CBlockData &crbd, int blocksize, int numOfblck8 ,int QstepDC, int QstepAC);
int DPCM_pix_0(unsigned char upper[][8], unsigned char current[][8], int *err_temp[8], int blocksize); // horizontal;;
int DPCM_pix_1(unsigned char left[][8], unsigned char current[][8], int *err_temp[8], int blocksize);  // vertical;
int DPCM_pix_2(unsigned char left[][8], unsigned char upper[][8], unsigned char current[][8], int *err_temp[8], int blocksize); // DC;
void IDPCM_pix(FrameData &frame);
void IDPCM_pix_0(unsigned char upper[][8], double current[][8], unsigned char restored_temp[][8], int blocksize);
void IDPCM_pix_1(unsigned char left[][8], double current[][8], unsigned char restored_temp[][8], int blocksize);
void IDPCM_pix_2(unsigned char left[][8], unsigned char upper[][8], double current[][8], unsigned char restored_temp[][8], int blocksize);


/* inter prediction function*/
int interPrediction(FrameData& n_1frame, FrameData& nframe, int QstepDC, int QstepAC);
void getPaddingImage(unsigned char* src, unsigned char* dst, int padWidth, int padlen, int width, int height);
void get16block(unsigned char* img, unsigned char dst[][16], int y, int x, int width, int blocksize);
int getSAD(unsigned char currentblck[][16], unsigned char spiralblck[][16], int blocksize);
void motionCompensation(FrameData& cntFrm, FrameData& prevFrm);
void motionEstimation(FrameData& cntFrm, FrameData& prevFrm);
void interYReconstruct(FrameData& cntFrm, FrameData& prevFrm);
void interCbCr(FrameData& cntFrm, FrameData& prevFrm, int QstepDC, int QstepAC);
void CmotionCompensation(FrameData& cntFrm, FrameData& prevFrm, int type);
void interCbCrReconstruct(FrameData& cntFrm, FrameData& prevFrm);
void mvPrediction(FrameData& cntFrm, int numOfblck16);
void ImvPrediction(FrameData& cntFrm, int numOfblck16);


/* check result function*/
void checkResultY(unsigned char* img, int width, int height);
void checkResultYUV(unsigned char *Y, unsigned char *Cb, unsigned char *Cr, int width, int height);
void checkResultFrames(FrameData* frm,char* fname, int width, int height, int nframe,int QstepDC, int QstepAC, int intraPeriod, int predtype, int chtype); //predtype : INTRA or INTER, chtype: SAVE_Y or SAVE_YUV
void checkRestructed(FrameData* frms, int nframes, int width, int height, int type);

/* common function */
void DPCM_DC_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth, int predmode);
void IDPCM_DC_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth, int predmode);
void DCT_block(BlockData &bd , int numOfblck8, int blocksize, int type);
void Quantization_block(BlockData &bd, int numOfblck8, int blocksize, int QstepDC, int QstepAC, int type);
void IQuantization_block(BlockData &bd, int numOfblck8, int blocksize, int QstepDC, int QstepAC, int type);
void IDCT_block(BlockData &bd, int numOfblck8, int blocksize, int type);
void mergeBlock(BlockData &bd, int blocksize, int type);
void CDCT_block(CBlockData &bd, int blocksize, int predmode);
void CQuantization_block(CBlockData &bd, int blocksize, int QstepDC, int QstepAC, int predmode);
void CIQuantization_block(CBlockData &bd, int blocksize, int QstepDC, int QstepAC, int predmode);
void CIDCT_block(CBlockData &bd, int blocksize, int predmode);
void CDPCM_DC_block(FrameData& frm, CBlockData& bd, int numOfblck8, int CbCrtype, int predmode);
void CIDPCM_DC_block(FrameData& frm, CBlockData& bd, int numOfblck8, int CbCrtype, int predmode);
void reordering(BlockData& bd, int numOfblck8, int predmode);
void zzf(Block8i &temp, int* dst, int nloop, int beginidx, int blocksize);
void zigzagScanning(Block8i &IQuanblck, int *dst, int blocksize);

void Creordering(CBlockData &bd,  int predmode);
void CzigzagScanning(Block8i *pQuanblck, int* dst, int blocksize);


/* Entropy function */
void entropyCoding(int* reordblck, int length);
void entropyCoding(FrameData& frm, int predmode);
void makebitstream(FrameData* frames, int nframes, int height, int width, int QstepDC, int QstepAC, int intraPeriod, int predmode, Statistics *stats = nullptr);
void headerinit(header& hd, int height, int width, int QstepDC, int QstepAC, int intraPeriod);
void allintraBody(FrameData* frames, int nframes, FILE* fp, Statistics *stats = nullptr);
void intraBody(FrameData& frm, unsigned char* tempFrame, int& cntbits, Statistics *stats = nullptr);
void interBody(FrameData& frm, unsigned char* tempFrame, int& cntbits, Statistics *stats = nullptr);
int DCentropy(int DCval, unsigned char *DCentropyResult);
unsigned char* DCentropy(int DCval, int& nbits, Statistics* stats = nullptr);
int ACentropy(int* reordblck, unsigned char *ACentropyResult);
unsigned char* ACentropy(int* reordblck, int& nbits, Statistics* stats = nullptr);
unsigned char* MVentropy(MotionVector mv, int& nbitsx, int& nbitsy, Statistics* stats = nullptr);

#endif //ICSP_CODEC_ENCODER
