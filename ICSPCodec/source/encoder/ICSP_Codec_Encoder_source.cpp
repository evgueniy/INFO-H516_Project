#include "ICSP_Codec_Encoder.h"
#include "ICSP_thread.h"
#include <cstring>
#include <pthread.h>

#include "abac/bitstream.h"
#include "abac/cabac.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <cstdlib>
#include "cabac.h"
using namespace std;

// Node for Huffman tree
struct HuffNode {
	int value;
	int freq;
	HuffNode* left;
	HuffNode* right;

	HuffNode(int v, int f) : value(v), freq(f), left(nullptr), right(nullptr) {}
	HuffNode(HuffNode* l, HuffNode* r) : value(-1), freq(l->freq + r->freq), left(l), right(r) {}
};

Statistics::Statistics(const int nframe) {
	frameCount = nframe;
	psnr = new double[nframe+1];
	totalAcBits = new unsigned[nframe];
	totalDcBits = new unsigned[nframe];
	totalMvBits = new unsigned[nframe];
	totalEntropyBits= new unsigned[nframe];
	mvPosX = new int*[nframe];
	mvPosY = new int*[nframe];
	mvDirX = new int*[nframe];
	mvDirY = new int*[nframe];
}

Statistics::~Statistics() {
	delete[] psnr, totalAcBits,totalDcBits, totalMvBits,totalEntropyBits;
	delete[] mvDirX,mvPosY,mvDirX,mvDirY;
}

// Comparator for priority queue
struct CompareNode {
	bool operator()(HuffNode* a, HuffNode* b) {
		return a->freq > b->freq;
	}
};

// Recursively build the Huffman codes
void buildCodes(HuffNode* node, const string& code, unordered_map<int, string>& codeMap) {
	if (!node) return;
	if (node->value >= 0) {
		codeMap[node->value] = code;
		return;
	}
	buildCodes(node->left, code + "0", codeMap);
	buildCodes(node->right, code + "1", codeMap);
}

// Clean up tree
void freeTree(HuffNode* node) {
	if (!node) return;
	freeTree(node->left);
	freeTree(node->right);
	delete node;
}

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

FILE* gfp;
char filename[256];

// The code is executed in build/[Debug|Release], so to have the
// results in the root directory, we go two levels up.
char resultDirectory[] = "../../results";
EntropyCoding EC = EntropyCoding::Original;
#ifdef WIN_MODE
/* benchmark function */
namespace TimeCheck
{
	double freq = 0.0;
	__int64 beginTime = 0;
	inline void TimeCheckStart()
	{
		LARGE_INTEGER tic;
		LARGE_INTEGER Qfreq;
		if (!QueryPerformanceFrequency(&Qfreq))
			cout << "Fail QueryPerformanceFrequency()!" << endl;

		freq = double(Qfreq.QuadPart) / 1000000.0;
		QueryPerformanceCounter(&tic);
		beginTime = tic.QuadPart;
	}

	inline double TimeCheckEnd()
	{
		LARGE_INTEGER toc;
		__int64 endTime = 0;
		double elapsedTime = 0;
		QueryPerformanceCounter(&toc);
		endTime = toc.QuadPart;
		elapsedTime = double(endTime - beginTime) / freq;
		return elapsedTime;
	}
}
#endif


/* message function */
void print_frame_end_message(int curr_frame_num, int frame_type)
{
	char frm_type = (frame_type == I_FRAME) ? 'I' : ((frame_type == P_FRAME) ? 'P' : 'B');
	printf("Encoding FRAME_%03d(%c) done!\n", curr_frame_num, frm_type);
}
void print_help_message()
{
	printf("usage: ./ICSPCodec [option] [values]\n");
	printf("-i : input yuv sequence\n");
	printf("-n : the number of frames(default is 1)\n");
	printf("-q : QP of DC and AC (16, 8, or 1)\n");
	printf("-h : set the height value (default CIF: 288)\n");
	printf("-w : set the width value (default CIF: 352)\n");
	printf("-e : set the entropy coder to use (original, abac, huffman or cabac - Default: original )\n");
	printf("--help : help message\n");
	printf("--qpdc : QP of DC (16, 8, or 1)\n");
	printf("--qpac : QP of AC (16, 8, or 1)\n");
	printf("--intraPeriod: period of intra frame(0: All intra)\n");
	printf("--EnMultiThread: enable multi threading mode, also the number of thread(0~4, 0 is disable)\n");
}
void print_error_message(int err_type, char* func_name)
{
	switch(err_type)
	{
		case UNENOUGH_PARAM:
			printf("[ERROR] unenough parameters in %s\n", func_name);
			break;
		case UNCORRECT_PARAM:
			printf("[ERROR] uncorrect parameters in %s\n", func_name);
			break;
		case FAIL_MEM_ALLOC:
			printf("[ERROR] fail memory allocation in %s\n", func_name);
			break;
		default:
			printf("[ERROR] unknown reason\n");
	}
	exit(-1);
}

void computePsnr(FrameData* frames,const int nframes,const int width, const int height ,Statistics *stats){
	double mse = 0,temp = 0;
	double total_mse = 0;
	for(int frm = 0; frm< nframes; frm++){
		mse = 0;
		for(int i = 0; i<width*height;i++){
			temp = (double)((int)frames[frm].Y[i] - (int)frames[frm].reconstructedY[i]);
			mse += (temp * temp);
		}
		total_mse += mse;
		mse /= height*width;
		stats->psnr[frm] = (mse > 0) ? 10 * log10((255 * 255)/mse) : INFINITY;
	}
	total_mse /= nframes * width * height;
	stats->psnr[nframes] = (mse > 0) ? 10 * log10((255 * 255)/total_mse) : INFINITY;
}

void writeFrameStats(const Statistics &stats, char* fname, int intra_period, int QstepDC, int QstepAC){
	char fileName[256];

	// File naming scheme original file name _ Qp _ Qp _ Intraperiod_encoder(0: original, 1: abac, 2: huffman, 3: cabac)
	sprintf(fileName, "%s/%s_%d_%d_%d_%d.csv",resultDirectory,fname,QstepDC,QstepAC,intra_period,static_cast<int>(EC));
	FILE *csv = fopen(fileName, "w");
	if (!csv) {
        perror("Error opening the CSV file");
        exit(1);
    }
	char line[256];
	//header
	//todo add PSNR per frame in header as well in data
    sprintf(line, "Frame;TotalDC;TotalAC;TotalMV;TotalEntropy;PSNR;AvgPSNR\n");
    fputs(line, csv);
	//data
	for(int i = 0; i< stats.frameCount;i++){
		sprintf(line, "%d;%u;%u;%u;%u;%.2f;%.2f\n", i, stats.totalDcBits[i], stats.totalAcBits[i], stats.totalMvBits[i], stats.totalEntropyBits[i],stats.psnr[i], stats.psnr[stats.frameCount]);
        fputs(line, csv);
	}
	fclose(csv);
}

void writeHistogramBitsizeStats(const Statistics &stats, char* fname, int intra_period, int QstepDC, int QstepAC){
	char fileName[256];

	// File naming scheme original file name _ Qp _ Qp _ Intraperiod 
	sprintf(fileName, "%s/hist_bitsize_%s_%d_%d_%d_%d.csv",resultDirectory,fname,QstepDC,QstepAC,intra_period,static_cast<int>(EC));
	FILE *csv = fopen(fileName, "w");
	if (!csv) {
        perror("Error opening the CSV file");
        exit(1);
    }
	char line[256];
	//header
	//todo add PSNR per frame in header as well in data
  sprintf(line, "Type;BitSize;Count\n");
  fputs(line, csv);

  int size = stats.histNbitsSize;
	//data
	for(int i = 0; i< size;i++){
		sprintf(line, "DC;%u;%u\n", i, stats.dcNbitsHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "AC;%u;%u\n", i, stats.acNbitsHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "MVX;%u;%u\n", i, stats.mvxNbitsHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "MVY;%u;%u\n", i, stats.mvyNbitsHistogram[i]);
    fputs(line, csv);
	}
	fclose(csv);
}

void writeHistogramValueStats(const Statistics &stats, char* fname, int intra_period, int QstepDC, int QstepAC){
	char fileName[256];

	// File naming scheme original file name _ Qp _ Qp _ Intraperiod 
	sprintf(fileName, "%s/hist_value_%s_%d_%d_%d_%d.csv",resultDirectory,fname,QstepDC,QstepAC,intra_period,static_cast<int>(EC));
	FILE *csv = fopen(fileName, "w");
	if (!csv) {
        perror("Error opening the CSV file");
        exit(1);
    }
	char line[256];
	//header
  sprintf(line, "Type;Value;Count\n");
  fputs(line, csv);

  int size = stats.histValueSize;
	//data
	for(int i = 0; i< size;i++){
		sprintf(line, "DC;%u;%u\n", i, stats.dcValuesHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "AC;%u;%u\n", i, stats.acValuesHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "MVX;%u;%u\n", i, stats.mvxValuesHistogram[i]);
    fputs(line, csv);
	}
	for(int i = 0; i< size;i++){
		sprintf(line, "MVY;%u;%u\n", i, stats.mvyValuesHistogram[i]);
    fputs(line, csv);
	}
	fclose(csv);
}

void writeMotionVectors(Statistics& stats, char* fname, int intraPeriod) {
	char fileName[256];

	sprintf(fileName, "%s/mv_%s_.csv", resultDirectory, fname);
	FILE *csv = fopen(fileName, "w");
	if (!csv) {
      perror("Error opening the CSV file");
      exit(1);
    }

	char line[256];
	//header
    sprintf(line, "Frame;MvPosX;MvPosY;MvDirX;MvDirY\n");
    fputs(line, csv);

    int frmCount = stats.frameCount;
    int blkCount = stats.numberOfBlocks;

	// First block
	for (int i = 0; i < blkCount; i++) {
		int posX = 0;
		int posY = 0;
		int dirX = 0;
		int dirY = 0;
		sprintf(line, "%d;%d;%d;%d;%d\n", 0, posX, posY, dirX, dirY);
		fputs(line, csv);
	}
	if (intraPeriod > 1){
		// Remaining blocks
		for (int frm = 1; frm < frmCount; frm++) {
			// Take the mv vectors of the previous frame (unless first which is all 0)
			int frmIdx = frm % intraPeriod == 0 ? frm - 1 : frm;
			for(int i = 0; i< blkCount; i++){
				int posX = stats.mvPosX[frmIdx][i];
				int posY = stats.mvPosY[frmIdx][i];
				int dirX = stats.mvDirX[frmIdx][i];
				int dirY = stats.mvDirY[frmIdx][i];
				sprintf(line, "%d;%d;%d;%d;%d\n", frm, posX, posY, dirX, dirY);
				fputs(line, csv);
			}
		}
	}
	fclose(csv);
}

/* parsing command function */
static void init_cmd_options(cmd_options_t* cmd)
{
	memset(cmd->yuv_fname, 0, sizeof(char)*256);
	cmd->QP_DC = 0;
	cmd->QP_AC = 0;
	cmd->intra_period = 0;
	cmd->multi_thread_mode = 0;
	memset(cmd->entropy_coder, 0, sizeof(char)*128);
}

// parse command and extract cfg options
static int parsing_command(int argc, char *argv[], cmd_options_t *cmd)
{
	if (argc < 2)
		return UNENOUGH_PARAM;	
	
	cmd -> height = 288;
	cmd -> width = 352;
	for(int i=1; i<argc; i++)
	{		
		char option[256];
		memcpy(option, argv[i], strlen(argv[i])+1);
		if (option[0] == '-')
		{
			if(option[1] == '-')
			{
				char long_name[256];
				memcpy(long_name, &option[2], strlen(&option[2])+1);
				if (strcmp(long_name, "qpdc") == 0)
				{
					cmd->QP_DC = atoi(argv[i+1]);
				}
				else if (strcmp(long_name, "qpac") == 0)
				{
					cmd->QP_AC = atoi(argv[i+1]);
				}
				else if (strcmp(long_name, "intraPeriod") == 0)
				{
					cmd->intra_period = atoi(argv[i+1]);
				}
				else if (strcmp(long_name, "EnMultiThread") == 0)
				{
					cmd->multi_thread_mode = atoi(argv[i+1]);
					cmd->nthreads = cmd->multi_thread_mode;
				}
				else if (strcmp(long_name, "help") == 0)
				{
					print_help_message();
					exit(0);
				}
				else
				{
					return UNCORRECT_PARAM;
				}
			}
			else
			{
				if (option[1] == 'i')
				{
					memcpy(cmd->yuv_fname, argv[i+1], sizeof(char)*256);
				}
				else if (option[1] == 'e')
				{
					memcpy(cmd->entropy_coder, argv[i+1], sizeof(char)*128);
					if(strcmp(cmd->entropy_coder,"original") != 0 && 
						strcmp(cmd->entropy_coder,"abac") != 0 && 
						strcmp(cmd->entropy_coder,"huffman") != 0 &&
						strcmp(cmd->entropy_coder,"cabac") != 0)
						sprintf(cmd->entropy_coder,"original");
				}
				else if (option[1] == 'n')
				{
					cmd->total_frames = atoi(argv[i+1]);
				}
				else if (option[1] == 'q')
				{
					cmd->QP_AC = atoi(argv[i+1]);
					cmd->QP_DC = atoi(argv[i+1]);
				}
				else if (option[1] =='h')
				{
					cmd->height = atoi(argv[i+1]);
				}
				else if (option[1] =='w')
				{
					cmd->width = atoi(argv[i+1]);
				}
				else
				{
					return UNCORRECT_PARAM;
				}
			}
		}
	}
	if(strcmp(cmd->entropy_coder,"") == 0) sprintf(cmd->entropy_coder,"original");
	printf("Testing e arg: %s\n",cmd->entropy_coder);
	return SUCCESS;
}
void set_command_options(int argc, char *argv[], cmd_options_t *cmd)
{

	init_cmd_options(cmd);

	int ret = parsing_command(argc, argv, cmd);
	if (ret != SUCCESS)
	{
		print_error_message(ret, "parsing_command");
	}
}

// multi-thread functions
void multi_thread_encoding(cmd_options_t* opt, FrameData* frames)
{
	thread_pool_t pool;
	thread_pool_init(&pool, opt->nthreads);
	thread_pool_start(&pool, opt->nthreads, frames, opt);
	thread_pool_end(&pool);;
}
void* encoding_thread(void* arg)
{
	thread_pool_t* pool = (thread_pool_t*)arg;
	queue<encoding_jobs_t> &Q = pool->job_queue;

	while(!Q.empty())
	{		
		pthread_mutex_lock(&pool->pool_mutex);
		encoding_jobs_t job = Q.front(); Q.pop();
		int intra_frame_num = job.start_frame_num;
		int end_frame_num = job.end_frame_num;
		int QP_DC = job.QP_DC;
		int QP_AC = job.QP_AC;		
		FrameData *pFrames = job.pFrames;
		pthread_mutex_unlock(&pool->pool_mutex);

		intraPrediction(pFrames[intra_frame_num], QP_DC, QP_AC);
		print_frame_end_message(intra_frame_num, I_FRAME);

		for(int inter_frame_num = intra_frame_num + 1; inter_frame_num <= end_frame_num; inter_frame_num++)
		{
			interPrediction(pFrames[inter_frame_num], pFrames[inter_frame_num-1], QP_DC, QP_AC);
			print_frame_end_message(inter_frame_num, P_FRAME);
		}
	}	

	return NULL;
}

// single-thread function
void single_thread_encoding(FrameData* frames, YCbCr_t* YCbCr,char* fname, int intra_period, int QstepDC, int QstepAC,char* entropyCoder, Statistics *stats)
{
	/*Use of global EntropyCoder var*/
	if(strcmp(entropyCoder,"original") == 0) EC = EntropyCoding::Original;
	else if(strcmp(entropyCoder,"abac") == 0) EC = EntropyCoding::Abac;
	else if(strcmp(entropyCoder,"huffman") == 0) EC = EntropyCoding::Huffman;
	else {
		EC = EntropyCoding::Cabac;
		x264_cabac_init();
	} 

	if( intra_period==ALL_INTRA )
	{
		allintraPrediction(frames, YCbCr->nframe, QstepDC, QstepAC);
		makebitstream(frames, YCbCr->nframe, YCbCr->height, YCbCr->width, QstepDC, QstepAC, intra_period, INTRA, stats);
		computePsnr(frames,YCbCr->nframe,YCbCr->width, YCbCr->height,stats);
		checkResultFrames(frames, fname,YCbCr->width, YCbCr->height, YCbCr->nframe, QstepDC, QstepAC, intra_period, INTRA, SAVE_YUV);
	}
	else
	{
		for(int n=0; n<YCbCr->nframe; n++)
		{
			int frame_type = 0;			
			if(n%intra_period==0)
			{
				frame_type = I_FRAME;
				intraPrediction(frames[n], QstepDC, QstepAC);				
			}
			else
			{
				frame_type = P_FRAME;
				interPrediction(frames[n], frames[n-1], QstepDC, QstepAC);
			}
			print_frame_end_message(n, frame_type);
		}		
		makebitstream(frames, YCbCr->nframe, YCbCr->height, YCbCr->width, QstepDC, QstepAC, intra_period, INTER, stats);
		computePsnr(frames,YCbCr->nframe,YCbCr->width, YCbCr->height,stats);
		checkResultFrames(frames, fname,YCbCr->width, YCbCr->height,YCbCr->nframe, QstepDC, QstepAC, intra_period, INTER, SAVE_YUV);
	}
}
/* initiation function*/
int YCbCrLoad(IcspCodec &icC, char* fname, const int nframe,  const int width, const int height)
{
	icC.YCbCr.nframe = nframe;
	icC.YCbCr.width  = width;
	icC.YCbCr.height = height;
	
	char CIF_fname[256];
	sprintf(CIF_fname, "%s", fname);

	FILE* input_fp;
	input_fp = fopen(CIF_fname, "rb");
	if(input_fp==NULL)
	{
		cout << "fail to load cif.yuv" << endl;
		return -1;
	}

	icC.YCbCr.Ys  = (unsigned char*) malloc(sizeof(unsigned char)*width*height*nframe);
	icC.YCbCr.Cbs = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2)*nframe);
	icC.YCbCr.Crs = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2)*nframe);

	if(icC.YCbCr.Ys == NULL || icC.YCbCr.Cbs == NULL || icC.YCbCr.Crs == NULL)
	{
		cout << "fail to malloc Ys, Cbs, Crs" << endl;
		return -1;
	}

	for(int i=0; i<nframe; i++)
	{
		fread(&icC.YCbCr.Ys[i*width*height], sizeof(unsigned char)*height*width, 1, input_fp);
		fread(&icC.YCbCr.Cbs[i*(width/2)*(height/2)], sizeof(unsigned char)*(width/2)*(height/2), 1, input_fp);
		fread(&icC.YCbCr.Crs[i*(width/2)*(height/2)], sizeof(unsigned char)*(width/2)*(height/2), 1, input_fp);
	}
	fclose(input_fp);

	return 0;
}
int splitFrames(IcspCodec &icC)
{
	int width  = icC.YCbCr.width;
	int height = icC.YCbCr.height;
	int nframe = icC.YCbCr.nframe;

	icC.frames = (FrameData *)malloc(sizeof(FrameData)*nframe);
	if(icC.frames == NULL)
	{
		cout << "fail to allocate memory to frames" << endl;
		return -1;
	}

	for(int numframe=0; numframe<nframe; numframe++)
	{
		FrameData &frm = icC.frames[numframe];
		frm.Y  = (unsigned char*) malloc(sizeof(unsigned char)*width*height);
		frm.Cb = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2));
		frm.Cr = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2));
		memcpy(frm.Y,  &icC.YCbCr.Ys[numframe*width*height], width*height);					  // Y ������ ���� ����; ���߿� �� ���� ����
		memcpy(frm.Cb, &icC.YCbCr.Cbs[numframe*(width/2)*(height/2)], (width/2)*(height/2));  // Cb ������ ���� ����;���߿� �� ���� ����
		memcpy(frm.Cr, &icC.YCbCr.Crs[numframe*(width/2)*(height/2)], (width/2)*(height/2));  // Cr ������ ���� ����;���߿� �� ���� ����	
		frm.numOfFrame = numframe;
	}

	return 0;
}
// \param blocksize1 Y blocks, typically 16x16
// \param blocksize2 UV blocks, typically 8x8
int splitBlocks(IcspCodec &icC, int blocksize1, int blocksize2)
{
	int width  = icC.YCbCr.width;
	int height = icC.YCbCr.height;
	int nframe = icC.YCbCr.nframe;	

	int splitWidth  = width  / blocksize1;	// �� ���������� ���ǰ˻�
	int splitHeight = height / blocksize1;  // �� ���������� ���ǰ˻�

	int totalblck = splitWidth * splitHeight;
	int nblck8 = icC.frames->nblocks8;
	if(width%blocksize1!=0 || height%blocksize1!=0)
	{
		cout << "improper value of blocksize1" << endl;
		return -1;
	}

	if(blocksize1%blocksize2!=0 || blocksize1%blocksize2!=0)
	{
		cout << "improper value of blocksize2" << endl;
		return -1;
	}

	// Number of block2 inside a block1
	int nblock2Ofblock1 = (int) (blocksize1*blocksize1) / (blocksize2*blocksize2);


	// We divide by 2 because we’re in YUV 4:2:0, so for every square of 4 pixels
	// we store 1 value of Cb and 1 value of Cr, while having 4 of Y
	int CbCrSplitWidth  = (icC.YCbCr.width  / 2) / blocksize2;
	int CbCrSplitHeight = (icC.YCbCr.height / 2) / blocksize2;
	int CbCrWidth  = (icC.YCbCr.width   / 2);
	int CbCrHeight = (icC.YCbCr.height  / 2);
	int index = 0;
	int cntx=0, cnty=0;
	for(int numframe=0; numframe<nframe; numframe++)
	{
		// Number of macro-blocks, of size 16x16
		icC.frames[numframe].blocks = (BlockData *) malloc(sizeof(BlockData)*splitWidth*splitHeight);	// �Ҹ��ڿ��� ��ȯ�����
		if(icC.frames[numframe].blocks == NULL)
		{
			cout << "frame[" << numframe << "].blocks malloc fail..." << endl;
			return -1;
		}

		FrameData &frm = icC.frames[numframe];
		// Y frames, fill with original values
		for(int numblock=0; numblock<totalblck; numblock++)
		{
			BlockData &bd = icC.frames[numframe].blocks[numblock];			

			// Index of upper left pixel of the current block
			cntx=(numblock%splitWidth)*blocksize1;
			cnty=(numblock/splitWidth)*blocksize1;

			// Fill with Y values of 16x16 block
			bd.originalblck16 = (Block16u*) malloc(sizeof(Block16u));
			for(int y=0; y<blocksize1; y++)
			{
				for(int x=0; x<blocksize1; x++)
				{	
					index = ((y*width)+(cnty*width))+(cntx)+x;
					bd.originalblck16->block[y][x] = frm.Y[index];
				}				
			}

			// Allocate ptrs and blocks for 8x8 blocks
			bd.originalblck8 = (Block8u**)malloc(sizeof(Block8u*)*nblock2Ofblock1);
			for(int i=0; i<nblock2Ofblock1; i++)
				bd.originalblck8[i] = (Block8u*)malloc(sizeof(Block8u));

			// The same data is essentially stored as in the 16x16 blocks, but
			// in 4 smaller blocks of 8x8. Still only the Y values
			for(int k=0; k<nblock2Ofblock1; k++)
			{
				for(int i=0; i<blocksize2; i++)
				{
					for(int j=0; j<blocksize2; j++)
					{
						if(k==0)
							bd.originalblck8[k]->block[i][j] = bd.originalblck16->block[i][j];
						if(k==1)
							bd.originalblck8[k]->block[i][j] = bd.originalblck16->block[i][j+blocksize2];
						if(k==2)
							bd.originalblck8[k]->block[i][j] = bd.originalblck16->block[i+blocksize2][j];
						if(k==3)
							bd.originalblck8[k]->block[i][j] = bd.originalblck16->block[i+blocksize2][j+blocksize2];
					}
				}				
			}	
			bd.numOfBlock16=numblock;
			bd.blocksize1 = blocksize1;
			bd.blocksize2 = blocksize2;
		}

		frm.nblocks16   = splitWidth*splitHeight;	// nblockxx �ɹ��� FrameData���� �ٸ� ����ü�� �ٲ��� ��ġ ����
		frm.nblocks8    = nblock2Ofblock1;
		frm.splitWidth  = splitWidth;
		frm.splitHeight = splitHeight;

		// Cb Cr frames
		icC.frames[numframe].Cbblocks = (CBlockData *) malloc(sizeof(CBlockData)*CbCrSplitWidth*CbCrSplitHeight);	// �Ҹ��ڿ��� ��ȯ�����
		icC.frames[numframe].Crblocks = (CBlockData *) malloc(sizeof(CBlockData)*CbCrSplitWidth*CbCrSplitHeight);	

		for(int numOfblck=0; numOfblck<CbCrSplitWidth*CbCrSplitHeight; numOfblck++)
		{
			// 8x8 ����ȭ
			CBlockData &cbbd = icC.frames[numframe].Cbblocks[numOfblck];
			CBlockData &crbd = icC.frames[numframe].Crblocks[numOfblck];

			cbbd.originalblck8 = (Block8u*)malloc(sizeof(Block8u));	// CDCT_block���� free
			crbd.originalblck8 = (Block8u*)malloc(sizeof(Block8u)); // CDCT_block���� free
			for(int y=0; y<blocksize2; y++)
			{
				for(int x=0; x<blocksize2; x++)
				{
					index = ((y*CbCrWidth)+(numOfblck/CbCrSplitWidth)*blocksize2*CbCrWidth)+(x+blocksize2*(numOfblck%CbCrSplitWidth));
					cbbd.originalblck8->block[y][x] = frm.Cb[index];
					crbd.originalblck8->block[y][x] = frm.Cr[index];
				}
			}
			cbbd.blocksize = blocksize2;
			crbd.blocksize = blocksize2;
		}
		frm.CbCrWidth = CbCrWidth;
		frm.CbCrHeight = CbCrHeight;
		frm.CbCrSplitWidth = CbCrSplitWidth;
		frm.CbCrSplitHeight = CbCrSplitHeight;
		frm.totalcbcrblck = CbCrWidth*CbCrHeight;
	}

	for(int i=0; i<icC.YCbCr.nframe; i++)
	{
		//free(icC.frames[i].Y);
		free(icC.frames[i].Cb);
		free(icC.frames[i].Cr);
	}

	return 0;
}

/* intra prediction function */
int allintraPrediction(FrameData* frames, int nframes, int QstepDC, int QstepAC)
{
	int totalblck = frames->nblocks16;
	int nblck8 = frames->nblocks8;
	int blocksize1 = frames->blocks->blocksize1;
	int blocksize2 = frames->blocks->blocksize2;
	int splitWidth = frames->splitWidth;
	int splitHeight = frames->splitHeight;

	
	for(int numOfFrm=0; numOfFrm<nframes; numOfFrm++)
	{
		//TimeCheck::TimeCheckStart();
		FrameData& frm = frames[numOfFrm];
		double DPCM_Time_PerFrame = 0;
		double DCT_Time_PerFrame = 0;
		double Quan_Time_PerFrame = 0;
		double IDPCM_Time_PerFrame = 0;
		double IDCT_Time_PerFrame = 0;

		for(int numOfblck16=0; numOfblck16<totalblck; numOfblck16++)
		{
			BlockData& bd = frm.blocks[numOfblck16];
			CBlockData& cbbd = frm.Cbblocks[numOfblck16];
			CBlockData& crbd = frm.Crblocks[numOfblck16];

			/* �Ҵ� ���� */
			bd.intraErrblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraErrblck[i] = (Block8i*)malloc(sizeof(Block8i));
			
			bd.intraDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));

			bd.intraQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

			bd.intraInverseQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraInverseQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

			bd.intraInverseDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraInverseDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));

			bd.intraRestructedblck8 = (Block8u**)malloc(sizeof(Block8u*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraRestructedblck8[i] = (Block8u*)malloc(sizeof(Block8u));

			cbbd.intraInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
			crbd.intraInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
			cbbd.intraInverseDCTblck  = (Block8d*)malloc(sizeof(Block8d));
			crbd.intraInverseDCTblck  = (Block8d*)malloc(sizeof(Block8d));
			/* �Ҵ� ���� �� */
			for(int numOfblck8=0; numOfblck8<nblck8; numOfblck8++)
			{
				//TimeCheck::TimeCheckStart();
				DPCM_pix_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth);
				//DPCM_Time_PerFrame += TimeCheck::TimeCheckEnd();
				DCT_block(bd, numOfblck8, blocksize2, INTRA);
				DPCM_DC_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth, INTRA);
				Quantization_block(bd, numOfblck8, blocksize2, QstepDC, QstepAC, INTRA);
				reordering(bd, numOfblck8, INTRA);
				IQuantization_block(bd, numOfblck8, blocksize2, QstepDC, QstepAC, INTRA);
				IDPCM_DC_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth, INTRA);
				IDCT_block(bd, numOfblck8, blocksize2, INTRA);
				IDPCM_pix_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth);
			}		
			intraCbCr(frm, cbbd, crbd, blocksize2, numOfblck16, QstepDC, QstepAC);	// 5th parameter, numOfblck16, is numOfblck8 in CbCr
			mergeBlock(bd, blocksize2, INTRA);

			/* free ���� */
			free(bd.originalblck8);
			free(bd.intraErrblck);
			free(bd.intraDCTblck);
			free(bd.intraQuanblck);			
			free(bd.intraInverseDCTblck);
			free(bd.originalblck16);
		}

		/*DPCM_Time_PerFrame /= (totalblck * nblck8);
		fprintf(gfp, "%lf\n", DPCM_Time_PerFrame);
		DPCM_Time_PerFrame = 0;*/

		intraImgReconstruct(frm);
		//entropyCoding(frm, INTRA);

		print_frame_end_message(numOfFrm, I_FRAME);
		
		/* free */
		for(int numOfblck16=0; numOfblck16<totalblck; numOfblck16++)
		{
			for(int numOfblck8=0; numOfblck8<nblck8; numOfblck8++)
				free(frm.blocks[numOfblck16].intraInverseQuanblck[numOfblck8]);
			free(frm.blocks[numOfblck16].intraInverseQuanblck);
			free(frm.Cbblocks[numOfblck16].intraInverseQuanblck);
			free(frm.Crblocks[numOfblck16].intraInverseQuanblck);
			free(frm.Cbblocks[numOfblck16].intraInverseDCTblck);
			free(frm.Crblocks[numOfblck16].intraInverseDCTblck);
		}
	}
	
	
	//fclose(gfp);
	// restructedY�� checkResultFrames���� ����ϹǷ� free�� ���߿� ����
	
	return 0;
}
void intraPrediction(FrameData& frm, int QstepDC, int QstepAC)
{
	// create Prediction blocks; mode 0: vertical 1: horizontal 2:DC	
	int totalblck = frm.nblocks16;
	int nblck8 = frm.nblocks8;
	int blocksize1 = frm.blocks->blocksize1;
	int blocksize2 = frm.blocks->blocksize2;
	int splitWidth = frm.splitWidth;
	int splitHeight = frm.splitHeight;
	
	for(int numOfblck16=0; numOfblck16<totalblck; numOfblck16++)
	{
		BlockData& bd = frm.blocks[numOfblck16];
		CBlockData& cbbd = frm.Cbblocks[numOfblck16];
		CBlockData& crbd = frm.Crblocks[numOfblck16];

		/* �Ҵ� ���� */
		bd.intraErrblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraErrblck[i] = (Block8i*)malloc(sizeof(Block8i));
		
		bd.intraDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));

		bd.intraQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

		bd.intraInverseQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraInverseQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

		bd.intraInverseDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblck8);
			for(int i=0; i<nblck8; i++)
				bd.intraInverseDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));

		bd.intraRestructedblck8 = (Block8u**)malloc(sizeof(Block8u*)*nblck8);	// free�� ���� �������� �����־�� �ϹǷ� �������� �̷�; ���� interprediction���꿡�� motioncompensation�� �����ǹǷ� �׶��� ����ϰ� free ��Ű�� �ɰ�
			for(int i=0; i<nblck8; i++)
				bd.intraRestructedblck8[i] = (Block8u*)malloc(sizeof(Block8u));
		
		cbbd.intraInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
		crbd.intraInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
		cbbd.intraInverseDCTblck = (Block8d*)malloc(sizeof(Block8d));
		crbd.intraInverseDCTblck = (Block8d*)malloc(sizeof(Block8d));
		/* �Ҵ� ���� �� */
		
		for(int numOfblck8=0; numOfblck8<nblck8; numOfblck8++)
		{
			DPCM_pix_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth);
			DCT_block(bd, numOfblck8, blocksize2, INTRA);
			DPCM_DC_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth, INTRA);
			Quantization_block(bd, numOfblck8, blocksize2, QstepDC, QstepAC, INTRA);

			reordering(bd, numOfblck8, INTRA);

			IQuantization_block(bd, numOfblck8, blocksize2, QstepDC, QstepAC, INTRA);
			IDPCM_DC_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth, INTRA);
			IDCT_block(bd, numOfblck8, blocksize2, INTRA);
			IDPCM_pix_block(frm, numOfblck16, numOfblck8, blocksize2, splitWidth);
		}		
		
		intraCbCr(frm, cbbd, crbd, blocksize2, numOfblck16, QstepDC, QstepAC); // numOfblck16 means numOfblck8 in CbCr Processing
		mergeBlock(bd, blocksize2, INTRA);		

		/* ���� ���� */
		free(bd.originalblck8);
		free(bd.intraErrblck);
		free(bd.intraDCTblck);
		free(bd.intraQuanblck);
		free(bd.intraInverseDCTblck);
		free(bd.originalblck16);

	}
	//printf("%.5f\n", (float)(clock() - start) / CLOCKS_PER_SEC);
	intraImgReconstruct(frm);

	for(int numOfblck16=0; numOfblck16<totalblck; numOfblck16++)
	{
		for(int numOfblck8=0; numOfblck8<nblck8; numOfblck8++)
			free(frm.blocks[numOfblck16].intraInverseQuanblck[numOfblck8]);
		free(frm.blocks[numOfblck16].intraInverseQuanblck);
		free(frm.Cbblocks[numOfblck16].intraInverseQuanblck);
		free(frm.Crblocks[numOfblck16].intraInverseQuanblck);
		free(frm.Cbblocks[numOfblck16].intraInverseDCTblck);
		free(frm.Crblocks[numOfblck16].intraInverseDCTblck);
	}
}
int DPCM_pix_0(unsigned char upper[][8], unsigned char current[][8], int *err_temp[8], int blocksize) // vertical; �ϴ� ù��° �ι�° �Ķ������ ���̸� 8�� static�ϰ� ����
{
	int SAE=0;	
	
	if(upper==NULL)
	{
		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				err_temp[y][x] = (int)current[y][x] - 128;
				SAE += abs(err_temp[y][x]);
			}
		}
	}
	else
	{
		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				err_temp[y][x] = (int)(current[y][x] - (int)upper[blocksize-1][x]);
				SAE += abs(err_temp[y][x]);
			}
		}
	}
	
	return SAE;
}
int DPCM_pix_1(unsigned char left[][8], unsigned char current[][8], int *err_temp[8], int blocksize) // horizontal; �ϴ� ù��° �ι�° �Ķ������ ���̸� 8�� static�ϰ� ����
{
	int SAE = 0;

	
	
	if (left == NULL)
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				err_temp[y][x] = (int)current[y][x] - 128;
				SAE += abs(err_temp[y][x]);
			}
		}
	}
	else
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				err_temp[y][x] = (int)current[y][x] - (int)left[y][blocksize - 1];
				SAE += abs(err_temp[y][x]);
			}
		}
	}
	
	return SAE;
}
int DPCM_pix_2(unsigned char left[][8], unsigned char upper[][8], unsigned char current[][8], int *err_temp[8], int blocksize) // DC; �ϴ� ù��° �ι�° �Ķ������ ���̸� 8�� static�ϰ� ����
{
	double predValLeft  = 0;
	double predValUpper = 0;
	double predVal      = 0;
	int SAE = 0;

	if (left == NULL)
	{
		predValLeft = 128 * 8;
	}
	else
	{
		for (int i = 0; i<blocksize; i++)
			predValLeft += left[i][blocksize - 1];
	}

	if (upper == NULL)
	{
		predValUpper = 128 * 8;
	}
	else
	{
		for (int i = 0; i<blocksize; i++)
			predValUpper += upper[blocksize - 1][i];
	}

	predVal = (predValLeft + predValUpper) / (double)(blocksize + blocksize);
	
	for (int y = 0; y<blocksize; y++)
	{
		for (int x = 0; x<blocksize; x++)
		{
			err_temp[y][x] = (int)current[y][x] - predVal;
			SAE += abs(err_temp[y][x]);
		}
	}

	return SAE;
}
void IDPCM_pix_0(unsigned char upper[][8], double current[][8], unsigned char restored_temp[][8], int blocksize)
{	
	
	int temp = 0;
	if (upper == NULL)
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				temp = current[y][x] + 128;
				temp = (temp > 255) ? 255 : temp;
				temp = (temp < 0) ? 0 : temp;
				restored_temp[y][x] = (unsigned char)temp;
			}
		}
	}
	else
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				temp = current[y][x] + upper[blocksize - 1][x];
				temp = (temp > 255) ? 255 : temp;
				temp = (temp < 0) ? 0 : temp;
				restored_temp[y][x] = (unsigned char)temp;
			}
		}
	}
	

}
void IDPCM_pix_1(unsigned char left[][8], double current[][8], unsigned char restored_temp[][8], int blocksize)
{
	
	int temp = 0;
	if (left == NULL)
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				temp = current[y][x] + 128;
				temp = (temp > 255) ? 255 : temp;
				temp = (temp < 0) ? 0 : temp;
				restored_temp[y][x] = temp;
			}
		}
	}
	else
	{
		for (int y = 0; y < blocksize; y++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				temp = current[y][x] + left[y][blocksize - 1];
				temp = (temp > 255) ? 255 : temp;
				temp = (temp < 0) ? 0 : temp;
				restored_temp[y][x] = temp;
				restored_temp[y][x] = temp;
			}
		}
	}
	
}
void IDPCM_pix_2(unsigned char left[][8], unsigned char upper[][8], double current[][8], unsigned char restored_temp[][8], int blocksize)
{
	int temp = 0;
	double predValLeft = 0;
	double predValUpper = 0;
	double predVal = 0;

	if (left == NULL)
	{
		predValLeft = 128 * 8;
	}
	else
	{
		for (int i = 0; i < blocksize; i++)
			predValLeft += left[i][blocksize - 1];
	}

	if (upper == NULL)
	{
		predValUpper = 128 * 8;
	}
	else
	{
		for (int i = 0; i < blocksize; i++)
			predValUpper += upper[blocksize - 1][i];
	}

	predVal = (predValLeft + predValUpper) / (blocksize + blocksize);
	
	for (int y = 0; y < blocksize; y++)
	{
		for (int x = 0; x < blocksize; x++)
		{
			temp = current[y][x] + predVal;
			temp = (temp > 255) ? 255 : temp;
			temp = (temp < 0) ? 0 : temp;
			restored_temp[y][x] = (unsigned char)temp;
		}
	}
	
}
void DPCM_pix_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth)
{
	int **temp0;
	int **temp1;
	int **temp2;
	temp0 = (int**)malloc(sizeof(int*)*blocksize);
	temp1 = (int**)malloc(sizeof(int*)*blocksize);
	temp2 = (int**)malloc(sizeof(int*)*blocksize);

	if (temp0 == NULL || temp1 == NULL || temp2 == NULL)
	{
		printf("Memory allocation error!\n");		
	}

	for(int i=0; i<blocksize; i++)
	{
		temp0[i] = (int*)calloc(blocksize, sizeof(int));
		temp1[i] = (int*)calloc(blocksize, sizeof(int));
		temp2[i] = (int*)calloc(blocksize, sizeof(int));
	}

	int SAE0=0, SAE1=0, SAE2=0;
	int m = 0; // SAE�� �ּҰ��� ����� ����
	int numOfCurrentBlck=0;
	int predmode1=0, predmode2=0, predmode3=0, cpredmode=0;
	int median=0;
	
	BlockData& bd = frm.blocks[numOfblck16];
	bd.intraPredMode[numOfblck8] = 0;	// �ʱ�ȭ
	if(numOfblck16 == 0) // 16x16������ ��ġ�� ù ��°�̴�
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			DPCM_pix_2(NULL, NULL, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			for(int i=0; i<blocksize; i++)
				memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			bd.DPCMmodePred[numOfCurrentBlck] = 2;
			bd.MPMFlag[numOfCurrentBlck] = 0;
			bd.intraPredMode[numOfCurrentBlck] = 0;
			break;
		case 1:
			numOfCurrentBlck=1;
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[0]->block, NULL, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if (SAE2>SAE1) 
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else // ���� ���� ???
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];			
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 2:
			numOfCurrentBlck=2;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE2 = DPCM_pix_2(NULL, bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if(SAE2>SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 3:
			numOfCurrentBlck=3;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=bd.DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[2];

			// median
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				//bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		}
	}
	else if(numOfblck16/splitWidth == 0) // 16x16�� ��ġ�� ù ���̴�.
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			SAE1 = DPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, NULL, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if (SAE2>SAE1) 
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else // ���� ���� ???
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode = bd.DPCMmodePred[numOfCurrentBlck];
			predmode1 = frm.blocks[numOfblck16-1].DPCMmodePred[1];
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 1:
			numOfCurrentBlck=1;
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[0]->block, NULL, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if (SAE2>SAE1) 
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else // ���� ���� ???
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode = bd.DPCMmodePred[numOfCurrentBlck];
			predmode1 = bd.DPCMmodePred[0];
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 2:
			numOfCurrentBlck=2;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=frm.blocks[numOfblck16-1].DPCMmodePred[3];
			predmode2=frm.blocks[numOfblck16-1].DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[0];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		case 3:
			numOfCurrentBlck=3;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=bd.DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[2];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}			
			break;
		}
	}
	else if(numOfblck16%splitWidth == 0) // 16x16 ������ ��ġ�� ù ���̴�.
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			SAE0 = DPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE2 = DPCM_pix_2(NULL, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if(SAE2>SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode = bd.DPCMmodePred[numOfCurrentBlck];
			predmode1 = frm.blocks[numOfblck16-splitWidth].DPCMmodePred[2];
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 1:
			numOfCurrentBlck=1;
			SAE0 = DPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[0]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=frm.blocks[numOfblck16-splitWidth].DPCMmodePred[2];
			predmode3=frm.blocks[numOfblck16-splitWidth].DPCMmodePred[3];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		case 2:
			numOfCurrentBlck=2;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE2 = DPCM_pix_2(NULL, bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			if(SAE2>SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode = bd.DPCMmodePred[numOfCurrentBlck];
			predmode1 = bd.DPCMmodePred[0];
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==predmode1)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(predmode1==0) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)? 0:1;
				else if(predmode1==2) 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
				else 
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)? 0:1;
			}
			break;
		case 3:
			numOfCurrentBlck=3;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=bd.DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[2];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (bd.DPCMmodePred[numOfCurrentBlck]==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (bd.DPCMmodePred[numOfCurrentBlck]==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		}
	}
	else // 16x16 ������ �� ���� ��ġ�� �ִ�.
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			SAE0 = DPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=frm.blocks[numOfblck16-1].DPCMmodePred[1];
			predmode2=frm.blocks[numOfblck16-splitWidth-1].DPCMmodePred[3];
			predmode3=frm.blocks[numOfblck16-splitWidth].DPCMmodePred[2];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		case 1:
			numOfCurrentBlck=1;
			SAE0 = DPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[0]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=frm.blocks[numOfblck16-splitWidth].DPCMmodePred[2];
			predmode3=frm.blocks[numOfblck16-splitWidth].DPCMmodePred[3];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		case 2:
			numOfCurrentBlck=2;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraRestructedblck8[0]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=frm.blocks[numOfblck16-1].DPCMmodePred[3];
			predmode2=frm.blocks[numOfblck16-1].DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[0];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		case 3:
			numOfCurrentBlck=3;
			SAE0 = DPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp0, blocksize);
			SAE1 = DPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.originalblck8[numOfCurrentBlck]->block, temp1, blocksize);
			SAE2 = DPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.originalblck8[numOfCurrentBlck]->block, temp2, blocksize);
			m = min(SAE0, SAE1); m = min(m, SAE2);
			if(m==SAE0)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 0;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp0[i], sizeof(int)*blocksize);
			}
			else if(m==SAE1)
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 1;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp1[i], sizeof(int)*blocksize);
			}
			else
			{
				bd.DPCMmodePred[numOfCurrentBlck] = 2;
				for(int i=0; i<blocksize; i++)
					memcpy(bd.intraErrblck[numOfCurrentBlck]->block[i], temp2[i], sizeof(int)*blocksize);
			}

			cpredmode=bd.DPCMmodePred[numOfCurrentBlck];
			predmode1=bd.DPCMmodePred[0];
			predmode2=bd.DPCMmodePred[1];
			predmode3=bd.DPCMmodePred[2];
			if( (predmode1>predmode2) && (predmode1>predmode3) )	 median=(predmode2>predmode3) ? predmode2:predmode3;
			else if((predmode2>predmode1) && (predmode2>predmode3))	 median=(predmode1>predmode3) ? predmode1:predmode3;
			else		   											 median=(predmode1>predmode2) ? predmode1:predmode2;
			bd.MPMFlag[numOfCurrentBlck] = (cpredmode==median)? 1:0;
			if(!bd.MPMFlag[numOfCurrentBlck])
			{
				if(median==0)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==1)?0:1;
				else if(median==2)
					bd.intraPredMode[numOfCurrentBlck] = (cpredmode==0)?0:1;
				else
					bd.intraPredMode[numOfCurrentBlck] = (median>cpredmode)? 0:1;
			}
			break;
		}
	}


	//fprintf(gfp, "%d\n", bd.DPCMmodePred[numOfCurrentBlck]);

	for(int i=0; i<blocksize; i++)
	{
		free(temp0[i]);
		free(temp1[i]);
		free(temp2[i]);
	}
	free(temp0);
	free(temp1);
	free(temp2);
	
	// restructedblck8 is needed to free here
	free(bd.originalblck8[numOfCurrentBlck]);
}
void IDPCM_pix_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth)
{
	int numOfCurrentBlck = 0;
	int predMode = 0;
	int modeblck0=0, modeblck1=0, modeblck2=0;
	int modetemp0=0, modetemp1=0, modetemp2=0;
	int median   = 0;
	BlockData &bd = frm.blocks[numOfblck16];
	if(numOfblck16==0) // 16x16 ù��° ����
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			predMode = 2; // ù���� ������ DPCM mode�� ������ 2
			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)      IDPCM_pix_0(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(NULL, NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 1:
			numOfCurrentBlck=1;
			modetemp0 = bd.IDPCMmode[0];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = 2;
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[0]->block, NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 2:
			numOfCurrentBlck=2;
			modetemp0 = bd.IDPCMmode[0];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = 2;
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(NULL, bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 3:
			numOfCurrentBlck=3;
			modeblck0 = bd.IDPCMmode[0];
			modeblck1 = bd.IDPCMmode[1];
			modeblck2 = bd.IDPCMmode[2];
			if( (modeblck0>modeblck1) && (modeblck0>modeblck2) )	 median=(modeblck1>modeblck2) ? modeblck1:modeblck2;
			else if((modeblck1>modeblck0) && (modeblck1>modeblck2))	 median=(modeblck0>modeblck2) ? modeblck0:modeblck2;
			else		   											 median=(modeblck0>modeblck1) ? modeblck0:modeblck1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
			{
				predMode = median;
			}
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}
			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		}
	}
	else if(numOfblck16/splitWidth == 0) // 16x16 ù ��
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			modetemp0=frm.blocks[numOfblck16-1].IDPCMmode[1];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = frm.blocks[numOfblck16-1].IDPCMmode[1];
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);

			break;
		case 1:
			numOfCurrentBlck=1;
			modetemp0=bd.IDPCMmode[0];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = bd.IDPCMmode[0];
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[0]->block, NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);

			break;
		case 2:
			numOfCurrentBlck=2;
			modetemp0 = bd.IDPCMmode[0];
			modetemp1 = frm.blocks[numOfblck16-1].IDPCMmode[1];
			modetemp2 = frm.blocks[numOfblck16-1].IDPCMmode[3];
			if( (modetemp0>modetemp1) && (modetemp0>modetemp2) )	 median=(modetemp1>modetemp2) ? modetemp1:modetemp2;
			else if((modetemp1>modetemp0) && (modetemp1>modetemp2))	 median=(modetemp0>modetemp2) ? modetemp0:modetemp2;
			else		   											 median=(modetemp0>modetemp1) ? modetemp0:modetemp1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck]=predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);

			break;
		case 3:
			numOfCurrentBlck=3;
			modeblck0 = bd.IDPCMmode[0];
			modeblck1 = bd.IDPCMmode[1];
			modeblck2 = bd.IDPCMmode[2];
			if( (modeblck0>modeblck1) && (modeblck0>modeblck2) )	 median=(modeblck1>modeblck2) ? modeblck1:modeblck2;
			else if((modeblck1>modeblck0) && (modeblck1>modeblck2))	 median=(modeblck0>modeblck2) ? modeblck0:modeblck2;
			else		   											 median=(modeblck0>modeblck1) ? modeblck0:modeblck1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);			
			break;
		}
	}
	else if(numOfblck16%splitWidth == 0) // 16x16 ù ��
	{
		switch(numOfblck8)
		{
		case 0:
			numOfCurrentBlck=0;
			modetemp0 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[2];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = frm.blocks[numOfblck16-splitWidth].IDPCMmode[2];
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}


			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(NULL, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 1:
			numOfCurrentBlck=1;
			modetemp0 = bd.IDPCMmode[0];
			modetemp1 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[2];
			modetemp2 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[3];
			if( (modetemp0>modetemp1) && (modetemp0>modetemp2) )	 median=(modetemp1>modetemp2) ? modetemp1:modetemp2;
			else if((modetemp1>modetemp0) && (modetemp1>modetemp2))	 median=(modetemp0>modetemp2) ? modetemp0:modetemp2;
			else		   											 median=(modetemp0>modetemp1) ? modetemp0:modetemp1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}


			bd.IDPCMmode[numOfCurrentBlck] = predMode;			
			if(predMode==0)		 IDPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[0]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 2:
			numOfCurrentBlck=2;
			modetemp0 = bd.IDPCMmode[0];
			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = bd.IDPCMmode[0];
			else
			{
				if(modetemp0==0)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;					
				else if(modetemp0==2)
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else
					predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}


			bd.IDPCMmode[numOfCurrentBlck] = predMode; 
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(NULL, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(NULL, bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 3:
			numOfCurrentBlck=3;
			modeblck0 = bd.IDPCMmode[0];
			modeblck1 = bd.IDPCMmode[1];
			modeblck2 = bd.IDPCMmode[2];
			if( (modeblck0>modeblck1) && (modeblck0>modeblck2) )	 median=(modeblck1>modeblck2) ? modeblck1:modeblck2;
			else if((modeblck1>modeblck0) && (modeblck1>modeblck2))	 median=(modeblck0>modeblck2) ? modeblck0:modeblck2;
			else		   											 median=(modeblck0>modeblck1) ? modeblck0:modeblck1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);	
			break;
		}
	}
	else // �׿��� ����
	{
		switch(numOfblck8)
		{		
		case 0:
			numOfCurrentBlck=0;
			modetemp0 = frm.blocks[numOfblck16-1].IDPCMmode[1];
			modetemp1 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[2];
			modetemp2 = frm.blocks[numOfblck16-splitWidth-1].IDPCMmode[3];
			if( (modetemp0>modetemp1) && (modetemp0>modetemp2) )	 median=(modetemp1>modetemp2) ? modetemp1:modetemp2;
			else if((modetemp1>modetemp0) && (modetemp1>modetemp2))	 median=(modetemp0>modetemp2) ? modetemp0:modetemp2;
			else		   											 median=(modetemp0>modetemp1) ? modetemp0:modetemp1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;	
			if(predMode==0)		 IDPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[1]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 1:
			numOfCurrentBlck=1;
			modetemp0 = bd.IDPCMmode[0];
			modetemp1 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[2];
			modetemp2 = frm.blocks[numOfblck16-splitWidth].IDPCMmode[3];
			if( (modetemp0>modetemp1) && (modetemp0>modetemp2) )	 median=(modetemp1>modetemp2) ? modetemp1:modetemp2;
			else if((modetemp1>modetemp0) && (modetemp1>modetemp2))	 median=(modetemp0>modetemp2) ? modetemp0:modetemp2;
			else		   											 median=(modetemp0>modetemp1) ? modetemp0:modetemp1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}
			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[0]->block, frm.blocks[numOfblck16-splitWidth].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 2:
			numOfCurrentBlck=2;
			modetemp0 = bd.IDPCMmode[0];
			modetemp1 = frm.blocks[numOfblck16-1].IDPCMmode[1];
			modetemp2 = frm.blocks[numOfblck16-1].IDPCMmode[3];
			if( (modetemp0>modetemp1) && (modetemp0>modetemp2) )	 median=(modetemp1>modetemp2) ? modetemp1:modetemp2;
			else if((modetemp1>modetemp0) && (modetemp1>modetemp2))	 median=(modetemp0>modetemp2) ? modetemp0:modetemp2;
			else		   											 median=(modetemp0>modetemp1) ? modetemp0:modetemp1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(frm.blocks[numOfblck16-1].intraRestructedblck8[3]->block, bd.intraRestructedblck8[0]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			break;
		case 3:
			numOfCurrentBlck=3;
			modeblck0 = bd.IDPCMmode[0];
			modeblck1 = bd.IDPCMmode[1];
			modeblck2 = bd.IDPCMmode[2];
			if( (modeblck0>modeblck1) && (modeblck0>modeblck2) )	 median=(modeblck1>modeblck2) ? modeblck1:modeblck2;
			else if((modeblck1>modeblck0) && (modeblck1>modeblck2))	 median=(modeblck0>modeblck2) ? modeblck0:modeblck2;
			else		   											 median=(modeblck0>modeblck1) ? modeblck0:modeblck1;

			if(bd.MPMFlag[numOfCurrentBlck]==1)
				predMode = median;
			else
			{
				if(median==0)       predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?1:2;
				else if(median==2)  predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:1;
				else				predMode = (bd.intraPredMode[numOfCurrentBlck]==0)?0:2;
			}

			bd.IDPCMmode[numOfCurrentBlck] = predMode;
			if(predMode==0)		 IDPCM_pix_0(bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==1) IDPCM_pix_1(bd.intraRestructedblck8[2]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);
			else if(predMode==2) IDPCM_pix_2(bd.intraRestructedblck8[2]->block, bd.intraRestructedblck8[1]->block, bd.intraInverseDCTblck[numOfCurrentBlck]->block, bd.intraRestructedblck8[numOfCurrentBlck]->block, blocksize);	
			break;
		}
	}

	free(bd.intraInverseDCTblck[numOfCurrentBlck]);
}
void intraCbCr(FrameData& frm, CBlockData &cbbd, CBlockData &crbd, int blocksize, int numOfblck8, int QstepDC, int QstepAC)
{
	// Cb
	cbbd.intraDCTblck = (Block8d*)malloc(sizeof(Block8d));
	cbbd.intraQuanblck = (Block8i*)malloc(sizeof(Block8i));

	CDCT_block(cbbd, blocksize, INTRA);
	CDPCM_DC_block(frm, cbbd, numOfblck8, CB, INTRA);
	CQuantization_block(cbbd, blocksize, QstepDC, QstepAC, INTRA);
	Creordering(cbbd, INTRA);
	CIQuantization_block(cbbd, blocksize, QstepDC, QstepAC, INTRA);
	CIDPCM_DC_block(frm, cbbd, numOfblck8, CB, INTRA);
	CIDCT_block(cbbd, blocksize, INTRA);	


	// Cr
	crbd.intraDCTblck = (Block8d*)malloc(sizeof(Block8d));
	crbd.intraQuanblck = (Block8i*)malloc(sizeof(Block8i));

	CDCT_block(crbd, blocksize, INTRA);
	CDPCM_DC_block(frm, crbd, numOfblck8, CR, INTRA);
	CQuantization_block(crbd, blocksize, QstepDC, QstepAC, INTRA);
	Creordering(crbd, INTRA);
	CIQuantization_block(crbd, blocksize, QstepDC, QstepAC, INTRA);
	CIDPCM_DC_block(frm, crbd, numOfblck8, CR, INTRA);
	CIDCT_block(crbd, blocksize, INTRA);

}
void intraImgReconstruct(FrameData &frm)
{
	int blocksize1  = frm.blocks->blocksize1;
	int blocksize2  = frm.blocks->blocksize2;
	int totalblck   = frm.nblocks16;
	int nblck8      = frm.nblocks8;
	int splitWidth  = frm.splitWidth;
	int splitHeight = frm.splitHeight;
	int CbCrSplitWidth  = frm.CbCrSplitWidth;
	int CbCrSplitHeight = frm.CbCrSplitHeight;
	int CbCrWidth  = frm.CbCrWidth;
	int CbCrHeight = frm.CbCrHeight;
	int width = splitWidth*blocksize1;
	int height = splitHeight*blocksize1;

	unsigned char* Ychannel  = (unsigned char*)calloc(width*height, sizeof(unsigned char));
	unsigned char* Cbchannel = (unsigned char*)calloc((width/2) * (height/2), sizeof(unsigned char));
	unsigned char* Crchannel = (unsigned char*)calloc((width/2) * (height/2), sizeof(unsigned char));

	frm.reconstructedY  = (unsigned char*) malloc(sizeof(unsigned char)*width*height);				//checkResultFrames�Լ����� ��ȯ
	frm.reconstructedCb = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2));		//checkResultFrames���� ��ȯ
	frm.reconstructedCr = (unsigned char*) malloc(sizeof(unsigned char)*(width/2)*(height/2));		//checkResultFrames���� ��ȯ

	int temp = 0;
	int tempCb = 0;
	int tempCr = 0;
	int nblck=0;	


	nblck=0;
	for(int y_interval=0; y_interval<splitHeight; y_interval++)
	{
		for(int x_interval=0; x_interval<splitWidth; x_interval++)
		{			
			BlockData &bd = frm.blocks[nblck]; // nblck++

			for(int y=0; y<blocksize1; y++)
			{
				for(int x=0; x<blocksize1; x++)
				{
					Ychannel[(y_interval*blocksize1*width) + (y*width) + (x_interval*blocksize1)+x] = bd.intraRestructedblck16.block[y][x];
				}
			}
			nblck++;
		}		
	}	
	memcpy(frm.reconstructedY, Ychannel, sizeof(unsigned char)*width*height);

	nblck=0;
	for(int y_interval=0; y_interval<CbCrSplitHeight; y_interval++)
	{
		for(int x_interval=0; x_interval<CbCrSplitWidth; x_interval++)
		{			
			CBlockData &cbbd = frm.Cbblocks[nblck]; // nblck++
			CBlockData &crbd = frm.Crblocks[nblck];
			int index=0;
			for(int y=0; y<blocksize2; y++)
			{
				for(int x=0; x<blocksize2; x++)
				{							
					tempCb = (cbbd.intraInverseDCTblck->block[y][x]>255) ? 255:cbbd.intraInverseDCTblck->block[y][x];							
					tempCb = (tempCb<0) ? 0 :tempCb;
					index = (y_interval*blocksize2*CbCrWidth) + (y*CbCrWidth) + (x_interval*blocksize2)+x;
					Cbchannel[index] = tempCb;

					tempCr = (crbd.intraInverseDCTblck->block[y][x]>255) ? 255:crbd.intraInverseDCTblck->block[y][x];
					tempCr = (tempCr<0) ? 0:tempCr;
					Crchannel[index] = tempCr;
				}
			}
			nblck++;
		}		
	}	
	memcpy(frm.reconstructedCb, Cbchannel, sizeof(unsigned char)*(width/2)*(height/2));
	memcpy(frm.reconstructedCr, Crchannel, sizeof(unsigned char)*(width/2)*(height/2));

	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);
}

/* inter prediction function */
int interPrediction(FrameData& cntFrm, FrameData& prevFrm, int QstepDC, int QstepAC)
{
	int totalblck = cntFrm.nblocks16;
	int nblock8 = cntFrm.nblocks8;
	int blocksize1 = cntFrm.blocks->blocksize1;
	int blocksize2 = cntFrm.blocks->blocksize2;
	int splitWidth = cntFrm.splitWidth;
	
	double MotionEstimation_PerFrame = 0;


	//TimeCheck::TimeCheckStart();
	motionEstimation(cntFrm, prevFrm);
	//MotionEstimation_PerFrame = TimeCheck::TimeCheckEnd();
	//fprintf(gfp, "%lf\n", MotionEstimation_PerFrame);
	//fclose(gfp);

	for(int nblck=0; nblck<totalblck; nblck++)
	{
		cntFrm.blocks[nblck].interErrblck16 = (Block16i*)malloc(sizeof(Block16i));
		cntFrm.blocks[nblck].interErrblck8 = (Block8i**)malloc(sizeof(Block8i*)*nblock8); // free�� DCT_block���� ����
		for(int i=0; i<nblock8; i++)
			cntFrm.blocks[nblck].interErrblck8[i] = (Block8i*)malloc(sizeof(Block8i));
	}
	
	motionCompensation(cntFrm, prevFrm);
	
	for(int nblck=0; nblck<totalblck; nblck++)
		free(cntFrm.blocks[nblck].interErrblck16);


	for(int nblck=0; nblck<totalblck; nblck++)
	{
		mvPrediction(cntFrm, nblck);

		/* �Ҵ�~ */
		cntFrm.blocks[nblck].interDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblock8);
		for(int i=0; i<nblock8; i++)
			cntFrm.blocks[nblck].interDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));
				
		cntFrm.blocks[nblck].interQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblock8);
		for(int i=0; i<nblock8; i++)
			cntFrm.blocks[nblck].interQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

		cntFrm.blocks[nblck].interInverseQuanblck = (Block8i**)malloc(sizeof(Block8i*)*nblock8);
		for(int i=0; i<nblock8; i++)
			cntFrm.blocks[nblck].interInverseQuanblck[i] = (Block8i*)malloc(sizeof(Block8i));

		cntFrm.blocks[nblck].interInverseDCTblck = (Block8d**)malloc(sizeof(Block8d*)*nblock8);
		for(int i=0; i<nblock8; i++)
			cntFrm.blocks[nblck].interInverseDCTblck[i] = (Block8d*)malloc(sizeof(Block8d));

		cntFrm.blocks[nblck].interInverseErrblck16 = (Block16i*)malloc(sizeof(Block16i));
		/* �Ҵ� ��*/

		for(int numOfblck8=0; numOfblck8<nblock8; numOfblck8++)
		{
			DCT_block(cntFrm.blocks[nblck], numOfblck8, blocksize2, INTER);	
			DPCM_DC_block(cntFrm, nblck, numOfblck8, blocksize2, splitWidth, INTER);
			Quantization_block(cntFrm.blocks[nblck], numOfblck8, blocksize2, QstepDC, QstepAC, INTER);
			reordering(cntFrm.blocks[nblck], numOfblck8, INTER);
			IQuantization_block(cntFrm.blocks[nblck], numOfblck8, blocksize2, QstepDC, QstepAC, INTER);
			IDPCM_DC_block(cntFrm, nblck, numOfblck8, blocksize2, splitWidth, INTER);
			IDCT_block(cntFrm.blocks[nblck], numOfblck8, blocksize2, INTER); // ��ȣ�� err���� ��ȯ
		}
		ImvPrediction(cntFrm, nblck);
		mergeBlock(cntFrm.blocks[nblck], blocksize2, INTER);		

		/* free */
		free(cntFrm.blocks[nblck].interDCTblck);
		free(cntFrm.blocks[nblck].interQuanblck);
		free(cntFrm.blocks[nblck].interInverseDCTblck);
	}
		
	interYReconstruct(cntFrm, prevFrm);
	interCbCr(cntFrm, prevFrm, QstepDC, QstepAC);
	//fclose(gfp);

	for(int nblck=0; nblck<totalblck; nblck++)
	{
		for(int i=0; i<nblock8; i++)
			free(cntFrm.blocks[nblck].interInverseQuanblck[i]);
		free(cntFrm.blocks[nblck].interInverseQuanblck);
		free(cntFrm.blocks[nblck].interInverseErrblck16);
	}
	return 0;
}
void motionEstimation(FrameData& cntFrm, FrameData& prevFrm)
{
	int padlen = cntFrm.blocks->blocksize1;
	int blocksize1 = cntFrm.blocks->blocksize1;
	int blocksize2 = cntFrm.blocks->blocksize2;
	int width  = cntFrm.splitWidth*cntFrm.blocks->blocksize1;	// 352
	int height = cntFrm.splitHeight*cntFrm.blocks->blocksize1;  // 288
	int padImgWidth  = width + padlen * 2;	//384
	int padImgHeight = height + padlen * 2;	//320
	int splitWidth = cntFrm.splitWidth;
	int splitHeight = cntFrm.splitHeight;

	unsigned char* paddingImage = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	// �е� �̹��� ����
	getPaddingImage(prevFrm.reconstructedY, paddingImage, padImgWidth, padlen, width, height);


	/*unsigned char** spiralblck = (unsigned char**)malloc(sizeof(unsigned char*)*blocksize1);
	for(int i=0; i<blocksize1; i++)	spiralblck[i] = (unsigned char*)malloc(sizeof(unsigned char)*blocksize1);*/
	unsigned char spiralblck[16][16] = { 0, };

	int y0=0, x0=0, cntX0=0, cntY0=0, tempX=0, tempY=0;
	int flag=0/*moving x or y*/, xflag=1/*moving x up or down*/, yflag=-1/*moving y up or down*/;
	int xcnt=0, ycnt=0;
	int totalblck = cntFrm.nblocks16;
	int SADflag=1;
	int SAD=0, min=INT_MAX;
	int cnt=0;
	int nSearch = 128; // spiral search ��ȸ��

	for(int nblck=0; nblck<totalblck && SADflag; nblck++)
	{	
		Block16u *currentblck = cntFrm.blocks[nblck].originalblck16;
		min = INT_MAX; SAD = 0;	cnt = 0; SADflag=1;
		cntX0 = x0 = (nblck%splitWidth)*blocksize1;
		cntY0 = y0 = (nblck/splitWidth)*blocksize1;
		xcnt=1; ycnt=1;
		flag=0, xflag=1, yflag=1;

		while(cnt<nSearch) // ��ü ȸ�� 64�� �̴���; ������ ���ɼ��� ũ��
		{	
			SAD = 0;

			get16block(paddingImage, spiralblck, (padlen+y0), (padlen+x0), padImgWidth, blocksize1);
			SAD = getSAD(currentblck->block, spiralblck, blocksize1);

			if(min > SAD)
			{
				min = SAD;
				tempX = x0;
				tempY = y0;
			}
			else if(SAD==0)
			{
				tempX = x0;
				tempY = y0;
				break;
			}

			cnt++;

			// Spiral search
			if (!flag) {
				// Move the x
				x0 += xflag;
				xcnt--;
				if (xcnt <= 0) {
					xcnt = ycnt + 1;
					flag = 1;
					xflag *= -1;
				}
			} else {
				// Move the y
				y0 += yflag;
				ycnt--;
				if (ycnt <= 0) {
					ycnt = xcnt;
					flag = 0;
					yflag *= -1;
				}
			}
		}
		cntFrm.blocks[nblck].mv.x = cntX0 - tempX;
		cntFrm.blocks[nblck].mv.y = cntY0 - tempY;

	}

	/*for(int i=0; i<blocksize1; i++)
		free(spiralblck[i]);
	free(spiralblck);*/

	free(paddingImage);	
}
void motionCompensation(FrameData& cntFrm, FrameData& prevFrm)
{
	// prev.restructedY�� �̿��ؼ� prediction block�� ������
	int totalblck  = cntFrm.nblocks16;
	int blocksize1 = cntFrm.blocks->blocksize1;
	int blocksize2 = cntFrm.blocks->blocksize2;
	int splitWidth = cntFrm.splitWidth;
	int width	   = cntFrm.splitWidth*blocksize1;
	int height	   = cntFrm.splitHeight*blocksize1;	
	int padlen = cntFrm.blocks->blocksize1;
	int padImgWidth  = width + padlen * 2;	//384
	int padImgHeight = height + padlen * 2;	//320


	/*unsigned char** predblck = (unsigned char**)malloc(sizeof(unsigned char*)*blocksize1);
	for(int i=0; i<blocksize1; i++) predblck[i] = (unsigned char *)malloc(sizeof(unsigned char)*blocksize1);*/
	unsigned char predblck[16][16] = { 0, };

	// �е� �̹��� ����
	unsigned char* paddingImage = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	getPaddingImage(prevFrm.reconstructedY, paddingImage, padImgWidth, padlen, width, height);

	int refx=0, refy=0, cntx=0, cnty=0;
	for(int nblck=0; nblck<totalblck; nblck++)
	{

		Block16u *cntblck = cntFrm.blocks[nblck].originalblck16;
		cntx = (nblck%splitWidth)*blocksize1;
		cnty = (nblck/splitWidth)*blocksize1;
		refx = cntx - cntFrm.blocks[nblck].mv.x + padlen;
		refy = cnty - cntFrm.blocks[nblck].mv.y + padlen;		

		get16block(paddingImage, predblck, refy, refx, padImgWidth, blocksize1);	// reconstructedY�� inter���� ������� �̹����� �ٲ������

		for(int y=0; y<blocksize1; y++)
		{
			for(int x=0; x<blocksize1; x++)
			{
				cntFrm.blocks[nblck].interErrblck16->block[y][x] = (int)(cntblck->block[y][x] - predblck[y][x]);
			}
		}

		for(int k=0; k<cntFrm.nblocks8; k++)
		{
			for(int y=0; y<blocksize2; y++)
			{
				for(int x=0; x<blocksize2; x++)
				{
					if(k==0)
						cntFrm.blocks[nblck].interErrblck8[k]->block[y][x] = cntFrm.blocks[nblck].interErrblck16->block[y][x];
					if(k==1)
						cntFrm.blocks[nblck].interErrblck8[k]->block[y][x] = cntFrm.blocks[nblck].interErrblck16->block[y][x+blocksize2];
					if(k==2)
						cntFrm.blocks[nblck].interErrblck8[k]->block[y][x] = cntFrm.blocks[nblck].interErrblck16->block[y+blocksize2][x];
					if(k==3)
						cntFrm.blocks[nblck].interErrblck8[k]->block[y][x] = cntFrm.blocks[nblck].interErrblck16->block[y+blocksize2][x+blocksize2];
				}
			}				
		}
	}


	for(int nblck=0; nblck<totalblck; nblck++)
		free(cntFrm.blocks[nblck].originalblck16);
	/*for(int i=0; i<blocksize1; i++) 
		free(predblck[i]);
	free(predblck);*/
	free(paddingImage);

	// ��⼭ restructedY�� free�ϸ� ������ ���Ŀ� checkResultFrames���� ����� Ȯ���ϱ����� restructedY�� �����ϹǷ� free�� �������� �̷���
}
void getPaddingImage(unsigned char* src, unsigned char* dst, int padWidth, int padlen, int width, int height)
{

	// Fill inside with initial image
	for(int y=0; y<height; y++)
		for(int x=0; x<width; x++)
			dst[(y*padWidth+padlen*padWidth)+(x+padlen)] = src[y*width+x];

	/*checkResultY(dst, 384, 320);
	cout << "getpad" << endl;
	system("pause");*/

	// Fill upper and bottom central pad band by extending edge
	for(int y=0; y<padlen; y++)
	{
		for(int x=0; x<width; x++)
		{
			dst[y*padWidth + (padlen+x)] = src[x];
			dst[(y+padlen+height-1)*padWidth + (x+padlen)] =src[(height-1)*width+x];
		}
	}

	// Fill left and right central pad band by extending edge
	for(int y=0; y<height; y++)
	{
		for(int x=0; x<padlen; x++)
		{
			// Fill left by extending edge
			dst[((y+padlen)*padWidth) + x] = src[y*width];
			// Fill right by extending edge
			dst[((y+padlen)*padWidth) + x+(width+padlen-1)] = src[y*width+(width-1)];
		}
	}

	// Fill 4 corners by extending the image corner value
	for(int y=0; y<padlen; y++)
	{
		for(int x=0; x<padlen; x++)
		{
			dst[y*padWidth + x] = src[0];
			dst[y*padWidth + x+(width+padlen-1)]  = src[width-1];
			dst[(y+padlen+height-1)*padWidth + x] = src[(height-1)*width];
			dst[(y+padlen+height-1)*padWidth + x+(padlen+width-1)] = src[height*width-1];
		}
	}
}
void get16block(unsigned char* img, unsigned char dst[][16], int y0, int x0, int width, int blocksize)  
{
	// padimg size - width: 382 height: 320
	// extract a 16x16 block at (x0, y0) coordinate	
	for (int y = 0; y < blocksize; y++)
	{
		for (int x = 0; x < blocksize; x++)
		{
			dst[y][x] = img[(y*width + y0*width) + x + x0];
		}
	}
	
}
int getSAD(unsigned char currentblck[][16], unsigned char spiralblck[][16], int blocksize)
{
	int SAD = 0;
	
	// 16 x 16
	for (int y = 0; y < blocksize; y++)
	{
		for (int x = 0; x < blocksize; x++)
		{
			SAD += abs((int)currentblck[y][x] - (int)spiralblck[y][x]);
		}
	}
	
	return SAD;
}
void interYReconstruct(FrameData& cntFrm, FrameData& prevFrm)
{
	int blocksize1  = cntFrm.blocks->blocksize1;
	int blocksize2  = cntFrm.blocks->blocksize2;
	int totalblck   = cntFrm.nblocks16;
	int nblck8      = cntFrm.nblocks8;
	int splitWidth  = cntFrm.splitWidth;
	int splitHeight = cntFrm.splitHeight;
	int width		= splitWidth*blocksize1;
	int height		= splitHeight*blocksize1;
	int temp		= 0;

	int padlen = cntFrm.blocks->blocksize1;
	int padImgWidth  = width  + padlen * 2;	//384
	int padImgHeight = height + padlen * 2;	//320	

	// �е� �̹��� ����
	unsigned char* paddingImage = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	getPaddingImage(prevFrm.reconstructedY, paddingImage, padImgWidth, padlen, width, height);

	unsigned char* Ychannel = (unsigned char*)calloc(width*height, sizeof(unsigned char));
	cntFrm.reconstructedY  = (unsigned char*) malloc(sizeof(unsigned char)*width*height);

	if (cntFrm.reconstructedY == NULL)
	{
		print_error_message(FAIL_MEM_ALLOC, "interYReconstruct");
	}

	int refx=0, refy=0, cntx=0, cnty=0;
	int cntindex=0, refindex=0;
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		BlockData &cntbd  = cntFrm.blocks[nblck];
		BlockData &prevbd = prevFrm.blocks[nblck];
		cntx = (nblck%splitWidth)*blocksize1;
		cnty = (nblck/splitWidth)*blocksize1;
		refx = cntx - cntFrm.blocks[nblck].Reconstructedmv.x + padlen;
		refy = cnty - cntFrm.blocks[nblck].Reconstructedmv.y + padlen;

		for(int y=0; y<blocksize1; y++)
		{
			for(int x=0; x<blocksize1; x++)
			{
				cntindex=((y*width)+(cnty*width))+(cntx)+ x;
				refindex=((y*padImgWidth)+(refy*padImgWidth))+(refx)+ x;
				temp = paddingImage[refindex] + cntbd.interInverseErrblck16->block[y][x];
				temp = (temp>255) ? 255:temp;
				temp = (temp<0)   ? 0  :temp;
				Ychannel[cntindex] = temp;
			}
		}			
	}
	memcpy(cntFrm.reconstructedY, Ychannel, sizeof(unsigned char)*width*height);
	free(Ychannel);
}
void mvPrediction(FrameData& cntFrm, int numOfblck16)
{
	int blocksize = cntFrm.blocks->blocksize1;
	int totalblck = cntFrm.nblocks16;
	int splitWidth = cntFrm.splitWidth;

	int x1=0, x2=0, x3=0, xmedian=0;
	int y1=0, y2=0, y3=0, ymedian=0;

	BlockData& bd = cntFrm.blocks[numOfblck16];

	// First block
	if(numOfblck16==0)
	{
		bd.mv.x = bd.mv.x-8;
		bd.mv.y = bd.mv.y-8;
	}
	// On first line
	else if(numOfblck16/splitWidth==0)
	{
		BlockData& prevbd = cntFrm.blocks[numOfblck16-1];
		bd.mv.x = bd.mv.x-prevbd.Reconstructedmv.x;
		bd.mv.y = bd.mv.y-prevbd.Reconstructedmv.y;
	}
	// On left edge
	else if(numOfblck16%splitWidth==0)
	{			
		BlockData& prevbd = cntFrm.blocks[numOfblck16-splitWidth];
		bd.mv.x = bd.mv.x-prevbd.Reconstructedmv.x;
		bd.mv.y = bd.mv.y-prevbd.Reconstructedmv.y;
	}
	else
	{			
		// On right edge
		if(numOfblck16%splitWidth==splitWidth-1)
		{
			// median l ul u
			x1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.x;			 // left
			x2 = cntFrm.blocks[numOfblck16-splitWidth-1].Reconstructedmv.x; // upper left
			x3 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.x;   // upper

			if((x1>x2)&&(x1>x3))	  xmedian = (x2>x3)?x2:x3;
			else if((x2>x1)&&(x2>x3)) xmedian = (x1>x3)?x1:x3;
			else					  xmedian = (x1>x2)?x1:x2;

			y1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.y;			 // left
			y2 = cntFrm.blocks[numOfblck16-splitWidth-1].Reconstructedmv.y; // upper left
			y3 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.y;   // upper

			if((y1>y2)&&(y1>y3))	  ymedian = (y2>y3)?y2:y3;
			else if((y2>y1)&&(y2>y3)) ymedian = (y1>x3)?y1:y3;
			else					  ymedian = (y1>y2)?y1:y2;
		}
		// Everything else
		else
		{
			// median l u ur
			x1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.x;				// left
			x2 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.x;		// upper
			x3 = cntFrm.blocks[numOfblck16-splitWidth+1].Reconstructedmv.x;    // upper right

			if((x1>x2)&&(x1>x3))	  xmedian = (x2>x3)?x2:x3;
			else if((x2>x1)&&(x2>x3)) xmedian = (x1>x3)?x1:x3;
			else					  xmedian = (x1>x2)?x1:x2;

			y1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.y;			   // left
			y2 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.y;	   // upper
			y3 = cntFrm.blocks[numOfblck16-splitWidth+1].Reconstructedmv.y;   // upper right

			if((y1>y2)&&(y1>y3))	  ymedian = (y2>y3)?y2:y3;
			else if((y2>y1)&&(y2>y3)) ymedian = (y1>x3)?y1:y3;
			else					  ymedian = (y1>y2)?y1:y2;
		}
		bd.mv.x = bd.mv.x-xmedian;
		bd.mv.y = bd.mv.y-ymedian;
	}
	//cout << "���� ���Ͱ�: " << bd.mv.x << ", " << bd.mv.y << endl;
}
void ImvPrediction(FrameData& cntFrm, int numOfblck16)
{
	int blocksize = cntFrm.blocks->blocksize1;
	int totalblck = cntFrm.nblocks16;
	int splitWidth =  cntFrm.splitWidth;
	int x1=0, x2=0, x3=0, xmedian=0;
	int y1=0, y2=0, y3=0, ymedian=0;


	BlockData& bd = cntFrm.blocks[numOfblck16];

	if(numOfblck16==0)
	{
		bd.Reconstructedmv.x = bd.mv.x+8;
		bd.Reconstructedmv.y = bd.mv.y+8;
	}
	else if(numOfblck16/splitWidth==0)
	{
		BlockData& prevbd = cntFrm.blocks[numOfblck16-1];
		bd.Reconstructedmv.x = bd.mv.x+prevbd.Reconstructedmv.x;
		bd.Reconstructedmv.y = bd.mv.y+prevbd.Reconstructedmv.y;
	}
	else if(numOfblck16%splitWidth==0)
	{			
		BlockData& prevbd = cntFrm.blocks[numOfblck16-splitWidth];
		bd.Reconstructedmv.x = bd.mv.x+prevbd.Reconstructedmv.x;
		bd.Reconstructedmv.y = bd.mv.y+prevbd.Reconstructedmv.y;
	}
	else
	{			
		if(numOfblck16%splitWidth==splitWidth-1)
		{
			// median l ul u
			x1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.x;			 // left
			x2 = cntFrm.blocks[numOfblck16-splitWidth-1].Reconstructedmv.x; // upper left
			x3 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.x;   // upper

			if((x1>x2)&&(x1>x3))	  xmedian = (x2>x3)?x2:x3;
			else if((x2>x1)&&(x2>x3)) xmedian = (x1>x3)?x1:x3;
			else					  xmedian = (x1>x2)?x1:x2;

			y1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.y;			 // left
			y2 = cntFrm.blocks[numOfblck16-splitWidth-1].Reconstructedmv.y; // upper left
			y3 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.y;   // upper

			if((y1>y2)&&(y1>y3))	  ymedian = (y2>y3)?y2:y3;
			else if((y2>y1)&&(y2>y3)) ymedian = (y1>x3)?y1:y3;
			else					  ymedian = (y1>y2)?y1:y2;
		}
		else
		{
			// median l u ur
			x1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.x;				// left
			x2 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.x;		// upper
			x3 = cntFrm.blocks[numOfblck16-splitWidth+1].Reconstructedmv.x;    // upper right

			if((x1>x2)&&(x1>x3))	  xmedian = (x2>x3)?x2:x3;
			else if((x2>x1)&&(x2>x3)) xmedian = (x1>x3)?x1:x3;
			else					  xmedian = (x1>x2)?x1:x2;

			y1 = cntFrm.blocks[numOfblck16-1].Reconstructedmv.y;			   // left
			y2 = cntFrm.blocks[numOfblck16-splitWidth].Reconstructedmv.y;	   // upper
			y3 = cntFrm.blocks[numOfblck16-splitWidth+1].Reconstructedmv.y;   // upper right

			if((y1>y2)&&(y1>y3))	  ymedian = (y2>y3)?y2:y3;
			else if((y2>y1)&&(y2>y3)) ymedian = (y1>x3)?y1:y3;
			else					  ymedian = (y1>y2)?y1:y2;
		}
		bd.Reconstructedmv.x = bd.mv.x+xmedian;
		bd.Reconstructedmv.y = bd.mv.y+ymedian;
	}
	//cout << "���� ���Ͱ�: " << bd.Reconstructedmv.x << ", " << bd.Reconstructedmv.y << endl;
	//system("pause");
}
void CmotionCompensation(FrameData& cntFrm, FrameData& prevFrm, int type)
{
	// padding image ����
	// mv�� �̿��� prediction block ����
	// ���п��� ����

	int totalblck  = cntFrm.nblocks16;
	int blocksize  = cntFrm.Cbblocks->blocksize;
	int splitWidth = cntFrm.CbCrSplitWidth;
	int width	   = cntFrm.CbCrSplitWidth*blocksize;
	int height	   = cntFrm.CbCrSplitHeight*blocksize;	
	int padlen     = cntFrm.Cbblocks->blocksize;	// padlen 8
	int padImgWidth  = width  + padlen * 2;		// padWidth  192
	int padImgHeight = height + padlen * 2;		// padHeight 160

	// make padding image
	unsigned char* paddingImage = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	if(type == CB)
		getPaddingImage(prevFrm.reconstructedCb, paddingImage, padImgWidth, padlen, width, height); //restruct�� ���� cb, cr������
	else if(type == CR)
		getPaddingImage(prevFrm.reconstructedCr, paddingImage, padImgWidth, padlen, width, height); //restruct�� ���� cb, cr������

	// create prediction block
	/*unsigned char** predblck = (unsigned char**)malloc(sizeof(unsigned char*)*blocksize);
	for(int i=0; i<blocksize; i++) predblck[i] = (unsigned char *)malloc(sizeof(unsigned char)*blocksize);*/
	unsigned char predblck[16][16] = { 0, };

	CBlockData *cbd = NULL;
	if(type == CB)
		cbd = cntFrm.Cbblocks;
	else if(type == CR)
		cbd = cntFrm.Crblocks;

	int refx=0, refy=0, cntx=0, cnty=0;
	for(int nblck=0; nblck<totalblck; nblck++)
	{		
		cntx = (nblck%splitWidth)*blocksize;
		cnty = (nblck/splitWidth)*blocksize;
		refx = cntx - (cntFrm.blocks[nblck].Reconstructedmv.x/2) + padlen;
		refy = cnty - (cntFrm.blocks[nblck].Reconstructedmv.y/2) + padlen;		

		get16block(paddingImage, predblck, refy, refx, padImgWidth, blocksize);	// reconstructedY�� inter���� ������� �̹����� �ٲ������

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				cbd[nblck].interErrblck->block[y][x] = (int)(cbd[nblck].originalblck8->block[y][x] - predblck[y][x]);
			}
		}		
	}	

	/*for(int i=0; i<blocksize; i++) free(predblck[i]);
	free(predblck);*/
	free(paddingImage);
	for(int i=0; i<totalblck; i++)
		free(cbd[i].originalblck8);
}
void interCbCrReconstruct(FrameData& cntFrm, FrameData& prevFrm)
{
	int blocksize   = cntFrm.Cbblocks->blocksize;
	int totalblck   = cntFrm.nblocks16;
	int splitWidth  = cntFrm.CbCrSplitWidth;
	int splitHeight = cntFrm.CbCrSplitHeight;
	int width		= splitWidth*blocksize;		// 176
	int height		= splitHeight*blocksize;	// 144


	int padlen		 = cntFrm.Cbblocks->blocksize; // 8
	int padImgWidth  = width  + padlen * 2;	//192
	int padImgHeight = height + padlen * 2;	//160	

	// �е� �̹��� ����
	unsigned char* paddingImageCb = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	getPaddingImage(prevFrm.reconstructedCb, paddingImageCb, padImgWidth, padlen, width, height);
	cntFrm.reconstructedCb = (unsigned char*) malloc(sizeof(unsigned char)*width*height);

	unsigned char* paddingImageCr = (unsigned char*) calloc(sizeof(unsigned char), padImgWidth*padImgHeight);
	getPaddingImage(prevFrm.reconstructedCr, paddingImageCr, padImgWidth, padlen, width, height);
	cntFrm.reconstructedCr = (unsigned char*) malloc(sizeof(unsigned char)*width*height);

	unsigned char* Cbchannel = (unsigned char*)calloc(width*height, sizeof(unsigned char));
	unsigned char* Crchannel = (unsigned char*)calloc(width*height, sizeof(unsigned char));

	int tempCb=0, tempCr=0;
	int refx=0, refy=0, cntx=0, cnty=0;
	int cntindex=0, refindex=0;
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		CBlockData &cntcbbd  = cntFrm.Cbblocks[nblck];
		CBlockData &prevcbbd = prevFrm.Cbblocks[nblck];
		CBlockData &cntcrbd  = cntFrm.Crblocks[nblck];
		CBlockData &prevcrbd = prevFrm.Crblocks[nblck];

		cntx = (nblck%splitWidth)*blocksize;
		cnty = (nblck/splitWidth)*blocksize;
		refx = cntx - (cntFrm.blocks[nblck].Reconstructedmv.x/2) + padlen;
		refy = cnty - (cntFrm.blocks[nblck].Reconstructedmv.y/2) + padlen;

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				cntindex=((y*width)+(cnty*width))+(cntx)+ x;
				refindex=((y*padImgWidth)+(refy*padImgWidth))+(refx)+ x;
				tempCb = paddingImageCb[refindex] + cntcbbd.interInverseDCTblck->block[y][x];
				tempCb = (tempCb>255) ? 255:tempCb;
				tempCb = (tempCb<0)   ? 0  :tempCb;
				Cbchannel[cntindex] = tempCb;

				tempCr = paddingImageCr[refindex] + cntcrbd.interInverseDCTblck->block[y][x];
				tempCr = (tempCr>255) ? 255:tempCr;
				tempCr = (tempCr<0)   ? 0  :tempCr;
				Crchannel[cntindex] = tempCr;
			}
		}			
	}
	memcpy(cntFrm.reconstructedCb, Cbchannel, sizeof(unsigned char)*width*height);
	memcpy(cntFrm.reconstructedCr, Crchannel, sizeof(unsigned char)*width*height);

	free(paddingImageCb);
	free(paddingImageCr);
	free(Cbchannel);
	free(Crchannel);
}
void interCbCr(FrameData& cntFrm, FrameData& prevFrm, int QstepDC, int QstepAC)
{
	int totalblck = cntFrm.CbCrSplitHeight*cntFrm.CbCrSplitWidth;
	int blocksize = cntFrm.Cbblocks->blocksize;

	// motionEstimation -> dont need ME because CbCr motion vector can be gotten by Y motion vector / 2
	for(int i=0; i<totalblck; i++)
	{
		cntFrm.Cbblocks[i].interErrblck = (Block8i*)malloc(sizeof(Block8i));
		cntFrm.Crblocks[i].interErrblck = (Block8i*)malloc(sizeof(Block8i));
		cntFrm.Cbblocks[i].interInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
		cntFrm.Crblocks[i].interInverseQuanblck = (Block8i*)malloc(sizeof(Block8i));
		cntFrm.Cbblocks[i].interInverseDCTblck = (Block8d*)malloc(sizeof(Block8d));
		cntFrm.Crblocks[i].interInverseDCTblck = (Block8d*)malloc(sizeof(Block8d));
	}

	CmotionCompensation(cntFrm, prevFrm, CB);
	CmotionCompensation(cntFrm, prevFrm, CR);

	// ���⼭���� ���ϴ��� �ݺ� 
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		// Cb �Ҵ� //
		cntFrm.Cbblocks[nblck].interDCTblck = (Block8d*)malloc(sizeof(Block8d));
		cntFrm.Cbblocks[nblck].interQuanblck = (Block8i*)malloc(sizeof(Block8i));

		CDCT_block(cntFrm.Cbblocks[nblck], blocksize, INTER);
		CDPCM_DC_block(cntFrm, cntFrm.Cbblocks[nblck], nblck, CB, INTER); 
		CQuantization_block(cntFrm.Cbblocks[nblck], blocksize, QstepDC, QstepAC, INTER);
		Creordering(cntFrm.Cbblocks[nblck], INTER);
		CIQuantization_block(cntFrm.Cbblocks[nblck], blocksize, QstepDC, QstepAC, INTER);
		CIDPCM_DC_block(cntFrm, cntFrm.Cbblocks[nblck], nblck, CB, INTER);
		CIDCT_block(cntFrm.Cbblocks[nblck], blocksize, INTER);


		// Cr �Ҵ� //
		cntFrm.Crblocks[nblck].interDCTblck = (Block8d*)malloc(sizeof(Block8d));
		cntFrm.Crblocks[nblck].interQuanblck = (Block8i*)malloc(sizeof(Block8i));

		CDCT_block(cntFrm.Crblocks[nblck], blocksize, INTER);
		CDPCM_DC_block(cntFrm, cntFrm.Crblocks[nblck], nblck, CR, INTER); 
		CQuantization_block(cntFrm.Crblocks[nblck], blocksize, QstepDC, QstepAC, INTER);
		Creordering(cntFrm.Crblocks[nblck], INTER);
		CIQuantization_block(cntFrm.Crblocks[nblck], blocksize, QstepDC, QstepAC, INTER);
		CIDPCM_DC_block(cntFrm, cntFrm.Crblocks[nblck], nblck, CR, INTER);
		CIDCT_block(cntFrm.Crblocks[nblck], blocksize, INTER);
	}

	interCbCrReconstruct(cntFrm, prevFrm);

	for(int i=0; i<totalblck; i++)
	{
		free(cntFrm.Cbblocks[i].interInverseQuanblck);
		free(cntFrm.Crblocks[i].interInverseQuanblck);
		free(cntFrm.Cbblocks[i].interInverseDCTblck);
		free(cntFrm.Crblocks[i].interInverseDCTblck);
	}
}

/* common function */
void DCT_block(BlockData &bd , int numOfblck8, int blocksize, int type)
{
	Block8d *DCTblck = NULL;
	Block8i *Errblck = NULL;
	Block8d temp;
	if(type==INTRA)
	{
		DCTblck = (bd.intraDCTblck[numOfblck8]);
		Errblck = (bd.intraErrblck[numOfblck8]);
	}
	else if(type==INTER)
	{
		DCTblck = (bd.interDCTblck[numOfblck8]);
		Errblck = (bd.interErrblck8[numOfblck8]);
	}

	for(int x=0; x<blocksize; x++)
		for(int y=0; y<blocksize; y++)
			DCTblck->block[y][x] = temp.block[y][x] = 0;
			

	
	// double type �õ� �ʿ�
	
	for (int v = 0; v < blocksize; v++)
	{
		for (int u = 0; u < blocksize; u++)
		{
			for (int x = 0; x < blocksize; x++)
			{
				temp.block[v][u] += (double)Errblck->block[v][x] * costable[u][x];
			}
		}
	}

	for (int u = 0; u < blocksize; u++)
	{
		for (int v = 0; v < blocksize; v++)
		{
			for (int y = 0; y < blocksize; y++)
			{
				DCTblck->block[v][u] += temp.block[y][u] * costable[v][y];
			}
		}
	}
	

	for (int i = 0; i<blocksize; i++)
	{
		DCTblck->block[0][i] *= irt2;
		DCTblck->block[i][0] *= irt2;
	}

	for (int i = 0; i<blocksize; i++)
	{
		for (int j = 0; j<blocksize; j++)
		{
			DCTblck->block[i][j] *= (1. / 4.);
		}
	}


	free(Errblck);
	
}
void Quantization_block(BlockData &bd, int numOfblck8, int blocksize, int QstepDC, int QstepAC, int type)
{
	Block8d *DCTblck = NULL;  
	Block8i *Quanblck = NULL; 
	int *ACflag = NULL;

	if(type==INTRA)
	{
		DCTblck  = (bd.intraDCTblck[numOfblck8]);
		Quanblck = (bd.intraQuanblck[numOfblck8]);
		ACflag = &(bd.intraACflag[numOfblck8]);
	}
	else if(type==INTER)
	{
		DCTblck  = (bd.interDCTblck[numOfblck8]);
		Quanblck = (bd.interQuanblck[numOfblck8]);
		ACflag = &(bd.interACflag[numOfblck8]);
	}

	
	for (int y = 0; y<blocksize; y++)
		for (int x = 0; x<blocksize; x++)
			Quanblck->block[y][x] = 0;

	int Qstep = 0;
	for (int y = 0; y<blocksize; y++)
	{
		for (int x = 0; x<blocksize; x++)
		{
			Qstep = (x == 0 && y == 0) ? QstepDC : QstepAC;
			Quanblck->block[y][x] = (int)(DCTblck->block[y][x] + 0.5) / Qstep;
		}
	}

	*ACflag = 1;
	for (int y = 0; y<blocksize && (*ACflag); y++)
	{
		for (int x = 0; x<blocksize && (*ACflag); x++)
		{
			if (x == 0 && y == 0) continue;
			*ACflag = (Quanblck->block[y][x] != 0) ? 0 : 1;
		}
	}
	

	free(DCTblck);
}
void IQuantization_block(BlockData &bd, int numOfblck8, int blocksize, int QstepDC, int QstepAC, int type)
{
	Block8i *Quanblck = NULL;
	Block8i *IQuanblck = NULL;
	if(type==INTRA)
	{
		Quanblck  = (bd.intraQuanblck[numOfblck8]);
		IQuanblck = (bd.intraInverseQuanblck[numOfblck8]);
	}
	else if(type==INTER)
	{
		Quanblck  = (bd.interQuanblck[numOfblck8]);
		IQuanblck = (bd.interInverseQuanblck[numOfblck8]);
	}
	
	int Qstep = 0;
	for (int y = 0; y < blocksize; y++)
	{
		for (int x = 0; x < blocksize; x++)
		{
			Qstep = (x == 0 && y == 0) ? QstepDC : QstepAC;
			IQuanblck->block[y][x] = Quanblck->block[y][x] * Qstep;
		}
	}
	
	// data saved in Quantization did not need after IQuantization Processing, So free
	free(Quanblck);
}
void IDCT_block(BlockData &bd, int numOfblck8, int blocksize, int type)
{
	Block8d *IDCTblck = NULL;
	Block8i *IQuanblck = NULL;
	Block8d temp;
	if(type==INTRA)
	{
		IDCTblck  = (bd.intraInverseDCTblck[numOfblck8]);
		IQuanblck = (bd.intraInverseQuanblck[numOfblck8]);
	}
	else if(type==INTER)
	{
		IDCTblck  = (bd.interInverseDCTblck[numOfblck8]);
		IQuanblck = (bd.interInverseQuanblck[numOfblck8]);
	}
	
	double *Cu = (double *)malloc(sizeof(double)*blocksize);
	double *Cv = (double *)malloc(sizeof(double)*blocksize);

	Cu[0] = Cv[0] = irt2;
	for (int i = 1; i<blocksize; i++)
	{
		Cu[i] = Cv[i] = 1.;
	}

	for (int y = 0; y<blocksize; y++)
	{
		for (int x = 0; x<blocksize; x++)
		{
			IDCTblck->block[y][x] = temp.block[y][x] = 0;
		}
	}

	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			for(int u=0; u<blocksize; u++)
			{
				temp.block[y][x] += Cu[u] * (double)IQuanblck->block[y][u] * costable[u][x];
			}
		}
	}

	for(int x=0; x<blocksize; x++)
	{
		for(int y=0; y<blocksize; y++)
		{
			for(int v=0; v<blocksize; v++)
			{
				IDCTblck->block[y][x] += Cv[v] * temp.block[v][x] * costable[v][y];
			}
		}
	}


	free(Cv);
	free(Cu);
	
	
	for (int i = 0; i<blocksize; i++)
	{
		for (int j = 0; j<blocksize; j++)
		{
			IDCTblck->block[i][j] *= (1. / 4.);
		}
	}
	
}
void reordering(BlockData &bd, int numOfblck8, int predmode)
{
	int blocksize = bd.blocksize2;
	int *reorderedblck = (int*) calloc(blocksize*blocksize, sizeof(int));	// �س��� ���߿� free; ���߿� ��� entropy ��ȯ�ϰ� free��Ű�� ��

	Block8i *Quanblck = NULL;
	if(predmode==INTRA)		 
	{
		Quanblck = (bd.intraQuanblck[numOfblck8]);
		bd.intraReorderedblck8[numOfblck8] = reorderedblck;
	}
	else if(predmode==INTER) 
	{
		Quanblck = (bd.interQuanblck[numOfblck8]);
		bd.interReorderedblck8[numOfblck8] = reorderedblck;
	}

	zigzagScanning(*Quanblck, reorderedblck, blocksize);
}
void Creordering(CBlockData &cbd,  int predmode)
{
	int blocksize = cbd.blocksize;
	int *reorderedblck = (int*) calloc(blocksize*blocksize, sizeof(int));	// �س��� ���߿� free; ���߿� ��� entropy ��ȯ�ϰ� free��Ű�� ��

	Block8i *Quanblck = NULL;
	if(predmode==INTRA)		 
	{
		Quanblck = (cbd.intraQuanblck);
		cbd.intraReorderedblck = reorderedblck;
	}
	else if(predmode==INTER) 
	{
		Quanblck = (cbd.interQuanblck);
		cbd.interReorderedblck = reorderedblck;
	}
		
	CzigzagScanning(Quanblck, reorderedblck, blocksize);
}
void CzigzagScanning(Block8i *pQuanblck, int* dst, int blocksize)
{	
	Block8i &Quanblck = *pQuanblck;

	/*for(int nloop=1; nloop<=7; nloop+=2)
	{
		zzf(Quanblck, dst, nloop, beginidx, blocksize);
		beginidx+=nloop*2+1;
	}

	
	for(int nloop=6; nloop>=0; nloop-=2)
	{
		zzf(Quanblck, dst, nloop, beginidx, blocksize);
		beginidx+=nloop*2+1;
	}*/
	dst[0]	=	Quanblck.block[0][0];
	dst[1]	=	Quanblck.block[0][1];
	dst[2]	=	Quanblck.block[1][0];
	dst[3]	=	Quanblck.block[2][0];
	dst[4]	=	Quanblck.block[1][1];
	dst[5]	=	Quanblck.block[0][2];
	dst[6]	=	Quanblck.block[0][3];
	dst[7]	=	Quanblck.block[1][2];
	dst[8]	=	Quanblck.block[2][1];
	dst[9]	=	Quanblck.block[3][0];
	dst[10]	=	Quanblck.block[4][0];
	dst[11]	=	Quanblck.block[3][1];
	dst[12]	=	Quanblck.block[2][2];
	dst[13]	=	Quanblck.block[1][3];
	dst[14]	=	Quanblck.block[0][4];
	dst[15]	=	Quanblck.block[0][5];
	dst[16]	=	Quanblck.block[1][4];
	dst[17]	=	Quanblck.block[2][3];
	dst[18]	=	Quanblck.block[3][2];
	dst[19]	=	Quanblck.block[4][1];
	dst[20]	=	Quanblck.block[5][0];
	dst[21]	=	Quanblck.block[6][0];
	dst[22]	=	Quanblck.block[5][1];
	dst[23]	=	Quanblck.block[4][2];
	dst[24]	=	Quanblck.block[3][3];
	dst[25]	=	Quanblck.block[2][4];
	dst[26]	=	Quanblck.block[1][5];
	dst[27]	=	Quanblck.block[0][6];
	dst[28]	=	Quanblck.block[0][7];
	dst[29]	=	Quanblck.block[1][6];
	dst[30]	=	Quanblck.block[2][5];
	dst[31]	=	Quanblck.block[3][4];
	dst[32]	=	Quanblck.block[4][3];
	dst[33]	=	Quanblck.block[5][2];
	dst[34]	=	Quanblck.block[6][1];
	dst[35]	=	Quanblck.block[7][0];
	dst[36]	=	Quanblck.block[7][1];
	dst[37]	=	Quanblck.block[6][2];
	dst[38]	=	Quanblck.block[5][3];
	dst[39]	=	Quanblck.block[4][4];
	dst[40]	=	Quanblck.block[3][5];
	dst[41]	=	Quanblck.block[2][6];
	dst[42]	=	Quanblck.block[1][7];
	dst[43]	=	Quanblck.block[2][7];
	dst[44]	=	Quanblck.block[3][6];
	dst[45]	=	Quanblck.block[4][5];
	dst[46]	=	Quanblck.block[5][4];
	dst[47]	=	Quanblck.block[6][3];
	dst[48]	=	Quanblck.block[7][2];
	dst[49]	=	Quanblck.block[7][3];
	dst[50]	=	Quanblck.block[6][4];
	dst[51]	=	Quanblck.block[5][5];
	dst[52]	=	Quanblck.block[4][6];
	dst[53]	=	Quanblck.block[3][7];
	dst[54]	=	Quanblck.block[4][7];
	dst[55]	=	Quanblck.block[5][6];
	dst[56]	=	Quanblck.block[6][5];
	dst[57]	=	Quanblck.block[7][4];
	dst[58]	=	Quanblck.block[7][5];
	dst[59]	=	Quanblck.block[6][6];
	dst[60]	=	Quanblck.block[5][7];
	dst[61]	=	Quanblck.block[6][7];
	dst[62]	=	Quanblck.block[7][6];
	dst[63]	=	Quanblck.block[7][7];

}
void zigzagScanning(Block8i &Quanblck, int* dst, int blocksize)
{
	int beginidx=0;
	int nloop=1;

	/*for(int nloop=1; nloop<=7; nloop+=2)
	{
		zzf(Quanblck, dst, nloop, beginidx, blocksize);
		beginidx+=nloop*2+1;
	}

	for(int nloop=6; nloop>=0; nloop-=2)
	{
		zzf(Quanblck, dst, nloop, beginidx, blocksize);
		beginidx+=nloop*2+1;
	}*/
	
	dst[0]	=	Quanblck.block[0][0];
	dst[1]	=	Quanblck.block[0][1];
	dst[2]	=	Quanblck.block[1][0];
	dst[3]	=	Quanblck.block[2][0];
	dst[4]	=	Quanblck.block[1][1];
	dst[5]	=	Quanblck.block[0][2];
	dst[6]	=	Quanblck.block[0][3];
	dst[7]	=	Quanblck.block[1][2];
	dst[8]	=	Quanblck.block[2][1];
	dst[9]	=	Quanblck.block[3][0];
	dst[10]	=	Quanblck.block[4][0];
	dst[11]	=	Quanblck.block[3][1];
	dst[12]	=	Quanblck.block[2][2];
	dst[13]	=	Quanblck.block[1][3];
	dst[14]	=	Quanblck.block[0][4];
	dst[15]	=	Quanblck.block[0][5];
	dst[16]	=	Quanblck.block[1][4];
	dst[17]	=	Quanblck.block[2][3];
	dst[18]	=	Quanblck.block[3][2];
	dst[19]	=	Quanblck.block[4][1];
	dst[20]	=	Quanblck.block[5][0];
	dst[21]	=	Quanblck.block[6][0];
	dst[22]	=	Quanblck.block[5][1];
	dst[23]	=	Quanblck.block[4][2];
	dst[24]	=	Quanblck.block[3][3];
	dst[25]	=	Quanblck.block[2][4];
	dst[26]	=	Quanblck.block[1][5];
	dst[27]	=	Quanblck.block[0][6];
	dst[28]	=	Quanblck.block[0][7];
	dst[29]	=	Quanblck.block[1][6];
	dst[30]	=	Quanblck.block[2][5];
	dst[31]	=	Quanblck.block[3][4];
	dst[32]	=	Quanblck.block[4][3];
	dst[33]	=	Quanblck.block[5][2];
	dst[34]	=	Quanblck.block[6][1];
	dst[35]	=	Quanblck.block[7][0];
	dst[36]	=	Quanblck.block[7][1];
	dst[37]	=	Quanblck.block[6][2];
	dst[38]	=	Quanblck.block[5][3];
	dst[39]	=	Quanblck.block[4][4];
	dst[40]	=	Quanblck.block[3][5];
	dst[41]	=	Quanblck.block[2][6];
	dst[42]	=	Quanblck.block[1][7];
	dst[43]	=	Quanblck.block[2][7];
	dst[44]	=	Quanblck.block[3][6];
	dst[45]	=	Quanblck.block[4][5];
	dst[46]	=	Quanblck.block[5][4];
	dst[47]	=	Quanblck.block[6][3];
	dst[48]	=	Quanblck.block[7][2];
	dst[49]	=	Quanblck.block[7][3];
	dst[50]	=	Quanblck.block[6][4];
	dst[51]	=	Quanblck.block[5][5];
	dst[52]	=	Quanblck.block[4][6];
	dst[53]	=	Quanblck.block[3][7];
	dst[54]	=	Quanblck.block[4][7];
	dst[55]	=	Quanblck.block[5][6];
	dst[56]	=	Quanblck.block[6][5];
	dst[57]	=	Quanblck.block[7][4];
	dst[58]	=	Quanblck.block[7][5];
	dst[59]	=	Quanblck.block[6][6];
	dst[60]	=	Quanblck.block[5][7];
	dst[61]	=	Quanblck.block[6][7];
	dst[62]	=	Quanblck.block[7][6];
	dst[63]	=	Quanblck.block[7][7];

}
void zzf(Block8i &temp, int* dst, int nloop, int beginidx, int blocksize)
{
	int len = blocksize-1;
	if(nloop%2)
	{
		for(int i=0; i<nloop; i++) dst[beginidx+i] = temp.block[nloop-i-1][i];
		dst[beginidx+nloop] = temp.block[0][nloop];
		for(int i=0; i<nloop; i++) dst[(beginidx+nloop*2)-i] = temp.block[nloop-i][i];
	}
	else
	{
		for(int i=0; i<nloop; i++) dst[beginidx+i]	= temp.block[len-i][len-nloop+i];
		dst[beginidx+nloop] = temp.block[len][len-nloop];
		for(int i=0; i<nloop; i++) dst[(beginidx+nloop*2)-i] = temp.block[len-i][len-nloop+i+1];
	}
}
void entropyCoding(int* reordblck, int length)
{
	int value=0;
	char sign=1;
	char category[13]={0};

	for(int i=0; i<length; i++)
	{
		sign = (value>=0) ? 1:0;
		value = abs(reordblck[i]);
		if(value == 0)					     category[0]++;
		else if(value == 1)				     category[1]++;
		else if(value>=2    &&  value<=3)    category[2]++;
		else if(value>=4    &&  value<=7)    category[3]++;
		else if(value>=8    &&  value<=15)   category[4]++;
		else if(value>=16   &&  value<=31)   category[5]++;
		else if(value>=32   &&  value<=63)   category[6]++;
		else if(value>=64   &&  value<=127)  category[7]++;
		else if(value>=128  &&  value<=255)  category[8]++;
		else if(value>=256  &&  value<=511)  category[9]++;
		else if(value>=512  &&  value<=1023) category[10]++;
		else if(value>=1024 &&  value<=2047) category[11]++;
		else if(value>=2048)				 category[12]++;
	}



	int totalsize =0;
	int totalbytes=0;
	int lastindex =0;
	totalsize  = category[0]*2;
	totalsize += category[1]*4;
	totalsize += category[2]*5;
	totalsize += category[3]*6;
	totalsize += category[4]*7;
	totalsize += category[5]*8;
	totalsize += category[6]*10;
	totalsize += category[7]*12;
	totalsize += category[8]*14;
	totalsize += category[9]*16;
	totalsize += category[10]*18;
	totalsize += category[11]*20;
	totalsize += category[12]*22;

	totalbytes = (totalsize/8)+1; // 8bit�� �� �������� ���� ��츦 ����� +1;
	lastindex  = totalbytes-1;
	unsigned char* bitchar = (unsigned char*)calloc(sizeof(unsigned char), totalsize);
	//unsigned char* entropyResult = (unsigned char*)calloc(sizeof(unsigned char), (totalsize/8)+2);
	unsigned char* entropyResult = (unsigned char*)calloc(sizeof(unsigned char), (totalsize/8)+1); // EOF�� byte�� ������



	// �ٽ� ���ư��鼭 ��Ʈ�� bitchar�� ä�� ������ �ǰڴ�
	int idx=0;
	int bitcnt=0;
	int c=0;
	int exp=0;
	unsigned char etp=0;
	for(int i=0; i<length; i++)
	{
		value = abs(reordblck[i]);
		sign = (reordblck[i]>=0) ? 1:0;
		if(value == 0)					  
		{
			bitchar[idx++]=0;
			bitchar[idx++]=0;
		}
		else if(value == 1)
		{
			bitchar[idx++]=0;
			bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;
		}
		else if(value>=2   &&  value<=3)
		{
			exp=1;
			bitchar[idx++]=0;
			bitchar[idx++]=1;
			bitchar[idx++]=1;
			bitchar[idx++]=sign;
			c=value-2;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;

		}
		else if(value>=4   &&  value<=7)
		{
			exp=2;
			bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-4;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=8   &&  value<=15)
		{
			exp=3;
			bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=1;
			bitchar[idx++]=sign;

			c=value-8;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=16  &&  value<=31)
		{
			exp=4;
			bitchar[idx++]=1;
			bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-16;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=32  &&  value<=63)
		{
			exp=5;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-32;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=64  &&  value<=127)
		{
			exp=6;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-64;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=128 &&  value<=255)
		{
			exp=7;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-128;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=256 &&  value<=511)
		{
			exp=8;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-256;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=512 &&  value<=1023)
		{
			exp=9;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-512;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=1024 &&  value<=2047)
		{
			exp=10;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-1024;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
		else if(value>=2048)
		{
			exp=11;
			for(int n=0; n<exp-2; n++)
				bitchar[idx++]=1;
			bitchar[idx++]=0;
			bitchar[idx++]=sign;

			c=value-2048;
			for(int n=exp; n>0; n--)
				bitchar[idx++] = (c>>(n-1))&1;
		}
	}


	unsigned char temp=0;
	for(int i=0; i<idx; i++)
	{
		temp |= bitchar[i];
		if((i+1)%8==0)
		{
			entropyResult[i/8] = temp;
		}		
		temp <<= 1;
	}
	temp >>= 1;

	entropyResult[totalbytes-1] = temp;

	int tmpbit=0;
	while((temp/=2) != 0)
		tmpbit++;

	entropyResult[totalbytes-1] <<= (8-tmpbit); // ������� ä���ֱ����� ������ ��Ʈ��ŭ shift ������
	//entropyResult[totalbytes-1] = EOF; // ���� ���� �ʾƵ� ���α׷��� ����ɶ� �˾Ƽ� EOF�� ���� �� ����

	/*cout << "original value" << endl;
	for(int i=0; i<length; i++)
	{
	cout << (int)reordblck[i] << " ";
	}
	cout << endl;

	cout << "entropy result" << endl;
	for(int i=0; i<totalbytes; i++)
	{
	cout << (int)entropyResult[i] << " ";
	}
	cout << endl;

	cout << "bitchar" << endl;
	for(int i=0; i<length; i++)
	{
	cout << (int)bitchar[i] << " ";
	}
	cout << endl;*/
	FILE* fp = fopen("entropy.txt", "a+");
	fwrite(entropyResult, sizeof(char), totalbytes, fp);	
	fclose(fp);

	free(entropyResult);	
}
void entropyCoding(FrameData& frm, int predmode)
{
	int totalblck = frm.nblocks16;
	int nblck8 = frm.nblocks8;
	int value = 0;
	int blocksize = frm.blocks->blocksize2;
	int zzlength  = blocksize*blocksize;
	int category[13] = {0};
	
	int *reordblck;
	
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		BlockData& bd = frm.blocks[nblck];
		for(int n8=0; n8<nblck8; n8++)
		{
			if(predmode==INTRA)
				reordblck = bd.intraReorderedblck8[n8];
			else if(predmode==INTER)
				reordblck = bd.interReorderedblck8[n8];

			for(int i=0; i<zzlength; i++)
			{
				value = abs(reordblck[i]);
				if(value == 0)					     category[0]++;
				else if(value == 1)				     category[1]++;
				else if(value>=2    &&  value<=3)    category[2]++;
				else if(value>=4    &&  value<=7)    category[3]++;
				else if(value>=8    &&  value<=15)   category[4]++;
				else if(value>=16   &&  value<=31)   category[5]++;
				else if(value>=32   &&  value<=63)   category[6]++;
				else if(value>=64   &&  value<=127)  category[7]++;
				else if(value>=128  &&  value<=255)  category[8]++;
				else if(value>=256  &&  value<=511)  category[9]++;
				else if(value>=512  &&  value<=1023) category[10]++;
				else if(value>=1024 &&  value<=2047) category[11]++;
				else if(value>=2048)				 category[12]++;
			}
		}
	}

	int totalbits  = 0;
	int totalbytes = 0;
	int lastindex  = 0;
	totalbits  = category[0]*2;
	totalbits += category[1]*4;
	totalbits += category[2]*5;
	totalbits += category[3]*6;
	totalbits += category[4]*7;
	totalbits += category[5]*8;
	totalbits += category[6]*10;
	totalbits += category[7]*12;
	totalbits += category[8]*14;
	totalbits += category[9]*16;
	totalbits += category[10]*18;
	totalbits += category[11]*20;
	totalbits += category[12]*22;

	//totalbytes = (totalbits/8)+1; // 8bit�� �� �������� ���� ��츦 ����� +1;
	//lastindex  = totalbytes-1;
	//bool* bitchar = (bool*)calloc(sizeof(bool), totalbits);
	//unsigned char* entropyResult = (unsigned char*)calloc(sizeof(unsigned char), (totalbits/8)+1); // EOF�� byte�� ������

	totalbytes = (totalbits/8)+2; // 8bit�� �� �������� ���� ��츦 ����� +1; EOF ������ ����Ʈ +1
	bool* bitchar = (bool*)calloc(sizeof(bool), totalbits);
	unsigned char* entropyResult = (unsigned char*)calloc(sizeof(unsigned char), totalbytes); // EOF�� byte����

	int idx=0;
	int bitcnt=0;
	int c=0;
	int exp=0;
	int sign  = 0;	
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		BlockData& bd = frm.blocks[nblck];
		for(int n8=0; n8<nblck8; n8++)
		{
			if(predmode==INTRA)
				reordblck = bd.intraReorderedblck8[n8];
			else if(predmode==INTER)
				reordblck = bd.interReorderedblck8[n8];

			for(int i=0; i<zzlength; i++)
			{
				value = abs(reordblck[i]);
				sign = (reordblck[i]>=0) ? 1:0;
				if(value == 0)					  
				{
					bitchar[idx++]=0;
					bitchar[idx++]=0;
				}
				else if(value == 1)
				{
					bitchar[idx++]=0;
					bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;
				}
				else if(value>=2   &&  value<=3)
				{
					exp=1;
					bitchar[idx++]=0;
					bitchar[idx++]=1;
					bitchar[idx++]=1;
					bitchar[idx++]=sign;
					c=value-2;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;

				}
				else if(value>=4   &&  value<=7)
				{
					exp=2;
					bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-4;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=8   &&  value<=15)
				{
					exp=3;
					bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=1;
					bitchar[idx++]=sign;

					c=value-8;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=16  &&  value<=31)
				{
					exp=4;
					bitchar[idx++]=1;
					bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-16;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=32  &&  value<=63)
				{
					exp=5;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-32;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=64  &&  value<=127)
				{
					exp=6;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-64;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=128 &&  value<=255)
				{
					exp=7;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-128;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=256 &&  value<=511)
				{
					exp=8;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-256;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=512 &&  value<=1023)
				{
					exp=9;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-512;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=1024 &&  value<=2047)
				{
					exp=10;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-1024;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
				else if(value>=2048)
				{
					exp=11;
					for(int n=0; n<exp-2; n++)
						bitchar[idx++]=1;
					bitchar[idx++]=0;
					bitchar[idx++]=sign;

					c=value-2048;
					for(int n=exp; n>0; n--)
						bitchar[idx++] = (c>>(n-1))&1;
				}
			}
		}
	}

	unsigned char temp=0;
	for(int i=0; i<idx; i++)
	{
		temp |= bitchar[i];
		if((i+1)%8==0)
		{
			entropyResult[i/8] = temp;
		}		
		temp <<= 1;
	}
	temp >>= 1;

	int tmpbit=0;
	while((temp/=2) != 0)
		tmpbit++;

	entropyResult[totalbytes-2] <<= (7-tmpbit); // ������� ä���ֱ����� ������ ��Ʈ��ŭ shift ������
	entropyResult[totalbytes-1] = EOF;
		
	FILE* fp = fopen("entropy.bin", "a+");
	fwrite(entropyResult, sizeof(unsigned char), totalbytes, fp);
	//cb cr �߰�
	fclose(fp);

	/*free*/
	for(int nblck=0; nblck<totalblck; nblck++)
	{
		BlockData& bd = frm.blocks[nblck];
		for(int n8=0; n8<nblck8; n8++)
		{
			if(predmode==INTRA)
				reordblck = bd.intraReorderedblck8[n8];
			else if(predmode==INTER)
				reordblck = bd.interReorderedblck8[n8];
			free(reordblck);
		}
	}
	free(bitchar);
	free(entropyResult);
}
void DPCM_DC_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth, int predmode)
{
	int a=0, b=0, c=0;
	int median = 0; 
	int numOfCurrentBlck=0;

	BlockData& bd = frm.blocks[numOfblck16];
	if(predmode==INTRA)
	{
		if(numOfblck16 == 0) // 16x16������ ��ġ�� ù ��°�̴�
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC value - 1024
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - 1024;
				break;
			case 1:
				numOfCurrentBlck=1;
				// DC value - DC value of left block
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// DC value - DC value of upper block
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// DC value - median value of DC value among left, upper left, upper right
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else if(numOfblck16/splitWidth == 0) // 16x16�� ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// dc - left dc
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - frm.blocks[numOfblck16-1].intraInverseQuanblck[1]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// dc - left dc
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// median
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[3]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper 
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else if(numOfblck16%splitWidth == 0) // 16x16 ������ ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC - upper
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur)
				a = bd.intraInverseQuanblck[0]->block[0][0]; // left
				b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper
				c = frm.blocks[numOfblck16-splitWidth+1].intraInverseQuanblck[2]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// upper
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				//median (l ul u)
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else // 16x16 ������ �� ���� ��ġ�� �ִ�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// median (l u ur)
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[1]->block[0][0];
				b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0];
				c = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0];
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur); �� ������ (l ul u)���� ����� ��
				if(numOfblck16%splitWidth == splitWidth-1)
				{
					a = bd.intraInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0]; // upper left
					c = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper 
				}
				else
				{
					a = bd.intraInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper
					c = frm.blocks[numOfblck16-splitWidth+1].intraInverseQuanblck[2]->block[0][0]; // upper right
				}
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// median (l u ur);
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[3]->block[0][0];	// left
				b = bd.intraInverseQuanblck[0]->block[0][0];	// upper
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper left
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median (l ul u)
				a = bd.intraInverseQuanblck[2]->block[0][0];	// left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper 
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraDCTblck[numOfCurrentBlck]->block[0][0] = bd.intraDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}		
	}
	else if(predmode==INTER)
	{
		if(numOfblck16 == 0) // 16x16������ ��ġ�� ù ��°�̴�
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC value - 1024
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - 1024;
				break;
			case 1:
				numOfCurrentBlck=1;
				// DC value - DC value of left block
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// DC value - DC value of upper block
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// DC value - median value of DC value among left, upper left, upper right
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else if(numOfblck16/splitWidth == 0) // 16x16�� ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// dc - left dc
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - frm.blocks[numOfblck16-1].interInverseQuanblck[1]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// dc - left dc
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// median
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[3]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper 
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else if(numOfblck16%splitWidth == 0) // 16x16 ������ ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC - upper
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur)
				a = bd.interInverseQuanblck[0]->block[0][0]; // left
				b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper
				c = frm.blocks[numOfblck16-splitWidth+1].interInverseQuanblck[2]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// upper
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				//median (l ul u)
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
		else // 16x16 ������ �� ���� ��ġ�� �ִ�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// median (l u ur)
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[1]->block[0][0];
				b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0];
				c = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0];
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur); �� ������ (l ul u)���� ����� ��
				if(numOfblck16%splitWidth == splitWidth-1)
				{
					a = bd.interInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0]; // upper left
					c = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper 
				}
				else
				{
					a = bd.interInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper
					c = frm.blocks[numOfblck16-splitWidth+1].interInverseQuanblck[2]->block[0][0]; // upper right
				}
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// median (l u ur);
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[3]->block[0][0];	// left
				b = bd.interInverseQuanblck[0]->block[0][0];	// upper
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper left
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median (l ul u)
				a = bd.interInverseQuanblck[2]->block[0][0];	// left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper 
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interDCTblck[numOfCurrentBlck]->block[0][0] = bd.interDCTblck[numOfCurrentBlck]->block[0][0] - median;
				break;
			}
		}
	}
}
void IDPCM_DC_block(FrameData &frm, int numOfblck16, int numOfblck8, int blocksize, int splitWidth, int predmode)
{
	int a=0, b=0, c=0;
	int median=0;
	int numOfCurrentBlck=0;

	BlockData& bd = frm.blocks[numOfblck16];

	if(predmode==INTRA)
	{
		if(numOfblck16 == 0) // 16x16������ ��ġ�� ù ��°�̴�
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC value - 1024
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + 1024;
				break;
			case 1:
				numOfCurrentBlck=1;
				// DC value - DC value of left block
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// DC value - DC value of upper block
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// DC value - median value of DC value among left, upper left, upper right
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c))		 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else if(numOfblck16/splitWidth == 0) // 16x16�� ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// dc - left dc
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + frm.blocks[numOfblck16-1].intraInverseQuanblck[1]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// dc - left dc
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// median
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[3]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper 
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else if(numOfblck16%splitWidth == 0) // 16x16 ������ ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// upper
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur)
				a = bd.intraInverseQuanblck[0]->block[0][0]; // left
				b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper
				c = frm.blocks[numOfblck16-splitWidth+1].intraInverseQuanblck[2]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// upper
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.intraInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// median (l ul u)
				a = bd.intraInverseQuanblck[2]->block[0][0]; // left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else // 16x16 ������ �� ���� ��ġ�� �ִ�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// median (l u ur)
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[1]->block[0][0];
				b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0];
				c = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0];
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur); �� ������ (l ul u)���� ����� ��
				if(numOfblck16%splitWidth == splitWidth-1)
				{
					a = bd.intraInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[2]->block[0][0]; // upper left
					c = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper 
				}
				else
				{
					a = bd.intraInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].intraInverseQuanblck[3]->block[0][0]; // upper
					c = frm.blocks[numOfblck16-splitWidth+1].intraInverseQuanblck[2]->block[0][0]; // upper right
				}
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// median (l u ur);
				a = frm.blocks[numOfblck16-1].intraInverseQuanblck[3]->block[0][0];	// left
				b = bd.intraInverseQuanblck[0]->block[0][0];	// upper
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper left
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 3:
				numOfCurrentBlck=3;
				a = bd.intraInverseQuanblck[2]->block[0][0];	// left
				b = bd.intraInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.intraInverseQuanblck[1]->block[0][0]; // upper 
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.intraInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
	}
	else if(predmode==INTER)
	{
		if(numOfblck16 == 0) // 16x16������ ��ġ�� ù ��°�̴�
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// DC value - 1024
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + 1024;
				break;
			case 1:
				numOfCurrentBlck=1;
				// DC value - DC value of left block
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// DC value - DC value of upper block
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// DC value - median value of DC value among left, upper left, upper right
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c))		 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else if(numOfblck16/splitWidth == 0) // 16x16�� ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// dc - left dc
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + frm.blocks[numOfblck16-1].interInverseQuanblck[1]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// dc - left dc
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 2:
				numOfCurrentBlck=2;
				// median
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[3]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper 
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 3:
				numOfCurrentBlck=3;
				// median
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else if(numOfblck16%splitWidth == 0) // 16x16 ������ ��ġ�� ù ���̴�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// upper
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0];
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur)
				a = bd.interInverseQuanblck[0]->block[0][0]; // left
				b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper
				c = frm.blocks[numOfblck16-splitWidth+1].interInverseQuanblck[2]->block[0][0]; // upper right
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// upper
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + bd.interInverseQuanblck[0]->block[0][0];
				break;
			case 3:
				numOfCurrentBlck=3;
				// median (l ul u)
				a = bd.interInverseQuanblck[2]->block[0][0]; // left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
		else // 16x16 ������ �� ���� ��ġ�� �ִ�.
		{
			switch(numOfblck8)
			{
			case 0:
				numOfCurrentBlck=0;
				// median (l u ur)
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[1]->block[0][0];
				b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0];
				c = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0];
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 1:
				numOfCurrentBlck=1;
				// median (l u ur); �� ������ (l ul u)���� ����� ��
				if(numOfblck16%splitWidth == splitWidth-1)
				{
					a = bd.interInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[2]->block[0][0]; // upper left
					c = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper 
				}
				else
				{
					a = bd.interInverseQuanblck[0]->block[0][0];	// left
					b = frm.blocks[numOfblck16-splitWidth].interInverseQuanblck[3]->block[0][0]; // upper
					c = frm.blocks[numOfblck16-splitWidth+1].interInverseQuanblck[2]->block[0][0]; // upper right
				}
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 2:
				numOfCurrentBlck=2;
				// median (l u ur);
				a = frm.blocks[numOfblck16-1].interInverseQuanblck[3]->block[0][0];	// left
				b = bd.interInverseQuanblck[0]->block[0][0];	// upper
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper left
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			case 3:
				numOfCurrentBlck=3;
				a = bd.interInverseQuanblck[2]->block[0][0];	// left
				b = bd.interInverseQuanblck[0]->block[0][0]; // upper left
				c = bd.interInverseQuanblck[1]->block[0][0]; // upper 
				if( (a>b) && (a>c) )	 median=(b>c) ? b:c;
				else if((b>a) && (b>c))	 median=(a>c) ? a:c;
				else		   			 median=(a>b) ? a:b;
				bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] = bd.interInverseQuanblck[numOfCurrentBlck]->block[0][0] + median;
				break;
			}
		}
	}
}
void CDCT_block(CBlockData &bd, int blocksize, int predmode)
{
	Block8d *DCTblck = NULL;
	Block8i temp;
	Block8i *Errblck = &temp;
	Block8d dcttemp;
	if(predmode==INTRA)
	{
		DCTblck = (bd.intraDCTblck);
		for(int y=0; y<blocksize; y++)
			for(int x=0; x<blocksize; x++)
				Errblck->block[y][x] = (int)bd.originalblck8->block[y][x];

	}
	else if(predmode==INTER)
	{
		DCTblck = (bd.interDCTblck);
		Errblck = (bd.interErrblck);
	}
		
	for(int y=0; y<blocksize; y++)
		for(int x=0; x<blocksize; x++)
			DCTblck->block[y][x] = dcttemp.block[y][x] = 0;

	/*for(int v=0; v<blocksize; v++)
	{
		for(int u=0; u<blocksize; u++)
		{
			for(int x=0; x<blocksize; x++)
			{
				for(int y=0; y<blocksize; y++)
				{
					DCTblck->block[v][u] += (double)Errblck->block[y][x] * cos(((2*x+1)*u*pi)/16.) * cos(((2*y+1)*v*pi)/16.);
				}
			}
		}
	}*/


	for(int v=0; v<blocksize; v++)
	{
		for(int u=0; u<blocksize; u++)
		{
			for(int x=0; x<blocksize; x++)
			{
				dcttemp.block[v][u] += (double)Errblck->block[v][x] * costable[u][x];
			}
		}
	}

	for(int u=0; u<blocksize; u++)
	{
		for(int v=0; v<blocksize; v++)
		{		
			for(int y=0; y<blocksize; y++)
			{
				DCTblck->block[v][u] += dcttemp.block[y][u] * costable[v][y];
			}
		}
	}

	for(int i=0; i<blocksize; i++)
	{
		DCTblck->block[0][i] *= irt2;
		DCTblck->block[i][0] *= irt2;
	}

	for(int i=0; i<blocksize; i++)
	{
		for(int j=0; j<blocksize; j++)
		{
			DCTblck->block[i][j] *= (1./4.);
		}
	}

	// splitBlocks���� �Ҵ�
	if(predmode==INTRA)
		free(bd.originalblck8);
	else if(predmode==INTER)
		free(Errblck);

}
void CDPCM_DC_block(FrameData& frm, CBlockData& bd, int numOfblck8, int CbCrtype, int predmode)
{
	int splitWidth = frm.CbCrSplitWidth;
	int numOfCurrentBlck =0;
	int a=0, b=0, c=0, median=0;

	CBlockData* cbd = NULL;
	if(CbCrtype==CB) cbd = frm.Cbblocks;
	else if(CbCrtype==CR) cbd = frm.Crblocks;

	Block8d *DCTblck = NULL;
	Block8i *IQuanblckLeft = NULL;
	Block8i *IQuanblckUpper = NULL;
	Block8i *IQuanblckUpperLeft = NULL;
	Block8i *IQuanblckUpperRight = NULL;
	if(predmode==INTRA)
	{
		if(numOfblck8==0)
			DCTblck = (bd.intraDCTblck);
		else if(numOfblck8/splitWidth==0)
		{
			DCTblck = (bd.intraDCTblck);
			IQuanblckLeft = (cbd[numOfblck8-1].intraInverseQuanblck);
		}
		else if(numOfblck8%splitWidth==0)
		{
			DCTblck = (bd.intraDCTblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].intraInverseQuanblck);
		}
		else 
		{
			DCTblck = (bd.intraDCTblck);
			IQuanblckLeft = (cbd[numOfblck8-1].intraInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].intraInverseQuanblck);
			IQuanblckUpperLeft = (cbd[numOfblck8-splitWidth-1].intraInverseQuanblck);
			IQuanblckUpperRight = (cbd[numOfblck8-splitWidth+1].intraInverseQuanblck);
		}
	}
	else if(predmode==INTER)
	{
		if(numOfblck8==0)
			DCTblck = (bd.interDCTblck);
		else if(numOfblck8/splitWidth==0)
		{
			DCTblck = (bd.interDCTblck);
			IQuanblckLeft = (cbd[numOfblck8-1].interInverseQuanblck);
		}
		else if(numOfblck8%splitWidth==0)
		{
			DCTblck = (bd.interDCTblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].interInverseQuanblck);
		}
		else 
		{
			DCTblck = (bd.interDCTblck);
			IQuanblckLeft = (cbd[numOfblck8-1].interInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].interInverseQuanblck);
			IQuanblckUpperLeft = (cbd[numOfblck8-splitWidth-1].interInverseQuanblck);
			IQuanblckUpperRight = (cbd[numOfblck8-splitWidth+1].interInverseQuanblck);
		}
	}

	if(numOfblck8==0)
	{
		DCTblck->block[0][0] = DCTblck->block[0][0] - 1024;
	}
	else if(numOfblck8/splitWidth==0) // ù ��
	{
		DCTblck->block[0][0] = DCTblck->block[0][0] - IQuanblckLeft->block[0][0];
	}
	else if(numOfblck8%splitWidth==0) // ù ��
	{
		DCTblck->block[0][0] = DCTblck->block[0][0] - IQuanblckUpper->block[0][0];
	}
	else
	{
		// median
		if(numOfblck8%splitWidth == splitWidth-1)
		{
			a = IQuanblckLeft->block[0][0];				// left
			b = IQuanblckUpperLeft->block[0][0];  // upper left
			c = IQuanblckUpper->block[0][0];    // upper
		}
		else
		{
			a = IQuanblckLeft->block[0][0];				// left
			b = IQuanblckUpper->block[0][0];	// upper 
			c = IQuanblckUpperRight->block[0][0];  // upper right
		}
		if((a>b)&&(a>c))		median=(b>c)?b:c;
		else if((b>a)&&(b>c))   median=(a>c)?a:c;
		else					median=(a>b)?a:b;
		DCTblck->block[0][0] = DCTblck->block[0][0] - median;
	}
}
void CIDPCM_DC_block(FrameData& frm, CBlockData& bd, int numOfblck8, int CbCrtype, int predmode)
{
	int splitWidth = frm.CbCrSplitWidth;
	int numOfCurrentBlck =0;
	int a=0, b=0, c=0, median=0;

	CBlockData* cbd = NULL;
	if(CbCrtype==CB) cbd = frm.Cbblocks;
	else if(CbCrtype==CR) cbd = frm.Crblocks;

	Block8i *IQuanblck = NULL;
	Block8i *IQuanblckLeft = NULL;
	Block8i *IQuanblckUpper = NULL;
	Block8i *IQuanblckUpperLeft = NULL;
	Block8i *IQuanblckUpperRight = NULL;
	if(predmode==INTRA)
	{
		if(numOfblck8==0)
			IQuanblck = (bd.intraInverseQuanblck);
		else if(numOfblck8/splitWidth==0)
		{
			IQuanblck = (bd.intraInverseQuanblck);
			IQuanblckLeft = (cbd[numOfblck8-1].intraInverseQuanblck);
		}
		else if(numOfblck8%splitWidth==0)
		{
			IQuanblck = (bd.intraInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].intraInverseQuanblck);
		}
		else
		{
			IQuanblck = (bd.intraInverseQuanblck);
			IQuanblckLeft = (cbd[numOfblck8-1].intraInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].intraInverseQuanblck);
			IQuanblckUpperLeft = (cbd[numOfblck8-splitWidth-1].intraInverseQuanblck);
			IQuanblckUpperRight = (cbd[numOfblck8-splitWidth+1].intraInverseQuanblck);
		}
	}
	else if(predmode==INTER)
	{
		if(numOfblck8==0)
			IQuanblck = (bd.interInverseQuanblck);
		else if(numOfblck8/splitWidth==0)
		{
			IQuanblck = (bd.interInverseQuanblck);
			IQuanblckLeft = (cbd[numOfblck8-1].interInverseQuanblck);
		}
		else if(numOfblck8%splitWidth==0)
		{
			IQuanblck = (bd.interInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].interInverseQuanblck);
		}
		else
		{
			IQuanblck = (bd.interInverseQuanblck);
			IQuanblckLeft = (cbd[numOfblck8-1].interInverseQuanblck);
			IQuanblckUpper = (cbd[numOfblck8-splitWidth].interInverseQuanblck);
			IQuanblckUpperLeft = (cbd[numOfblck8-splitWidth-1].interInverseQuanblck);
			IQuanblckUpperRight = (cbd[numOfblck8-splitWidth+1].interInverseQuanblck);
		}
	}

	if(numOfblck8==0)
	{
		IQuanblck->block[0][0] = IQuanblck->block[0][0] + 1024;
	}
	else if(numOfblck8/splitWidth==0) // ù ��
	{
		IQuanblck->block[0][0] = IQuanblck->block[0][0] + IQuanblckLeft->block[0][0];
	}
	else if(numOfblck8%splitWidth==0) // ù ��
	{
		IQuanblck->block[0][0] = IQuanblck->block[0][0] + IQuanblckUpper->block[0][0];
	}
	else
	{
		// median
		if(numOfblck8%splitWidth == splitWidth-1)
		{
			a = IQuanblckLeft->block[0][0];		 // left
			b = IQuanblckUpperLeft->block[0][0]; // upper left
			c = IQuanblckUpper->block[0][0];	 // upper
		}
		else
		{
			a = IQuanblckLeft->block[0][0];		    // left
			b = IQuanblckUpper->block[0][0];		// upper 
			c = IQuanblckUpperRight->block[0][0];   // upper right
		}
		if((a>b)&&(a>c))		median=(b>c)?b:c;
		else if((b>a)&&(b>c))   median=(a>c)?a:c;
		else					median=(a>b)?a:b;
		IQuanblck->block[0][0] = IQuanblck->block[0][0] + median;
	}
}
void CQuantization_block(CBlockData &bd, int blocksize, int QstepDC, int QstepAC, int predmode)
{
	Block8d *DCTblck = NULL;
	Block8i *Quanblck = NULL;
	int *ACflag = NULL;
	if(predmode==INTRA)
	{
		DCTblck  = (bd.intraDCTblck);
		Quanblck = (bd.intraQuanblck);
		ACflag = &(bd.intraACflag);
	}
	else if(predmode==INTER)
	{
		DCTblck  = (bd.interDCTblck);
		Quanblck = (bd.interQuanblck);
		ACflag = &(bd.interACflag);
	}

	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			Quanblck->block[y][x] = 0;
		}
	}

	int Qstep = 0;
	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			Qstep = (x==0&&y==0)? QstepDC:QstepAC;
			Quanblck->block[y][x] = (int)floor(DCTblck->block[y][x] + 0.5) / Qstep  ;
		}
	}

	*ACflag = 1;
	for(int y=0; y<blocksize && (*ACflag); y++)
	{
		for(int x=0; x<blocksize && (*ACflag); x++)
		{
			if(x==0&&y==0) continue;
			*ACflag = (Quanblck->block[y][x]!=0)?0:1;
		}
	}
	free(DCTblck);
}
void CIQuantization_block(CBlockData &bd, int blocksize, int QstepDC, int QstepAC, int predmode)
{
	Block8i *IQuanblck = NULL;
	Block8i *Quanblck = NULL;
	if(predmode==INTRA)
	{
		IQuanblck = (bd.intraInverseQuanblck);
		Quanblck = (bd.intraQuanblck);
	}
	else if(predmode==INTER)
	{
		IQuanblck = (bd.interInverseQuanblck);
		Quanblck = (bd.interQuanblck);
	}
	for(int y=0; y<blocksize; y++)
		for(int x=0; x<blocksize; x++)
			IQuanblck->block[y][x] = 0;

	int Qstep = 0;
	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			Qstep = (x==0&&y==0)? QstepDC:QstepAC;
			IQuanblck->block[y][x] = Quanblck->block[y][x] * Qstep;
		}
	}

	free(Quanblck);
}
void CIDCT_block(CBlockData &bd, int blocksize, int predmode)
{
	Block8d *IDCTblck = NULL;
	Block8i *IQuanblck = NULL;
	Block8d temp;
	if(predmode==INTRA)
	{
		IDCTblck = (bd.intraInverseDCTblck);
		IQuanblck = (bd.intraInverseQuanblck);
	}
	else if(predmode==INTER)
	{
		IDCTblck  = (bd.interInverseDCTblck);
		IQuanblck = (bd.interInverseQuanblck);
	}

	double *Cu = (double *)malloc(sizeof(double)*blocksize);
	double *Cv = (double *)malloc(sizeof(double)*blocksize);

	Cu[0] = Cv[0] = irt2;
	for(int i=1; i<blocksize; i++)
	{
		Cu[i] = Cv[i] = 1.;
	}

	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			IDCTblck->block[y][x] = temp.block[y][x] = 0;
		}			
	}

	//for(int y=0; y<blocksize; y++)
	//{
	//	for(int x=0; x<blocksize; x++)
	//	{
	//		for(int v=0; v<blocksize; v++)
	//		{
	//			for(int u=0; u<blocksize; u++)
	//			{
	//				IDCTblck->block[y][x] += Cu[u]*Cv[v] * (double)IQuanblck->block[v][u] * cos(((2*x+1)*u*pi)/16.) * cos(((2*y+1)*v*pi)/16.);   // �����δ� bd.intraDCTblck[n].block[u][v]�� �ƴ϶� InverseQuanblck�� ����
	//			}
	//		}
	//	}
	//}



	for(int y=0; y<blocksize; y++)
	{
		for(int x=0; x<blocksize; x++)
		{
			for(int u=0; u<blocksize; u++)
			{
				temp.block[y][x] += Cu[u] * (double)IQuanblck->block[y][u] * costable[u][x];//cos(((2*x+1)*u*pi)/16.) * cos(((2*y+1)*v*pi)/16.);
			}
		}
	}

	for(int x=0; x<blocksize; x++)
	{
		for(int y=0; y<blocksize; y++)
		{		
			for(int v=0; v<blocksize; v++)
			{
				IDCTblck->block[y][x] += Cv[v] * temp.block[v][x] * costable[v][y];//cos(((2*x+1)*u*pi)/16.) * cos(((2*y+1)*v*pi)/16.);
			}
		}
	}

	for(int i=0; i<blocksize; i++)
	{
		for(int j=0; j<blocksize; j++)
		{
			IDCTblck->block[i][j] *= (1./4.);
		}
	}

	free(Cv);
	free(Cu);
}
void mergeBlock(BlockData &bd, int blocksize, int type) // 8x8 -> 16x16 ���� �����
{	
	int nblck8 = 4;
	if(type==INTRA)
	{
		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.intraRestructedblck16.block[y][x] = bd.intraRestructedblck8[0]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.intraRestructedblck16.block[y][x+blocksize] = bd.intraRestructedblck8[1]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.intraRestructedblck16.block[y+blocksize][x] = bd.intraRestructedblck8[2]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.intraRestructedblck16.block[y+blocksize][x+blocksize] = bd.intraRestructedblck8[3]->block[y][x];
			}
		}
	}
	else if(type==INTER)
	{
		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.interInverseErrblck16->block[y][x] = (int)bd.interInverseDCTblck[0]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.interInverseErrblck16->block[y][x+blocksize] = (int)bd.interInverseDCTblck[1]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.interInverseErrblck16->block[y+blocksize][x] = (int)bd.interInverseDCTblck[2]->block[y][x];
			}
		}

		for(int y=0; y<blocksize; y++)
		{
			for(int x=0; x<blocksize; x++)
			{
				bd.interInverseErrblck16->block[y+blocksize][x+blocksize] = (int)bd.interInverseDCTblck[3]->block[y][x];
			}
		}		

		for(int i=0; i<nblck8; i++)
		{
			free(bd.interInverseDCTblck[i]);
		}
	}
}


/* bitstream function */
void makebitstream(FrameData* frames, int nframes, int height, int width, int QstepDC, int QstepAC, int intraPeriod, int predmode, Statistics *stats)
{
	/* header */
	header hd;
	// std::cout << (EC == EntropyCoding::Original ? "OG" : "Not OG") << std::endl;
	// std::cout << (EC == EntropyCoding::Cabac ? "CABAC" : "Not CABAC") << std::endl;
	// std::cout << (EC == EntropyCoding::Huffman ? "HF" : "Not HF") << std::endl;
	headerinit(hd, height, width, QstepDC, QstepAC, intraPeriod);
	char compCIFfname[256];
	char format[8];
	sprintf(format,"%s",((width == 352) ? "CIF" : ((width < 352) ? "QCIF" : "4CIF")));
	sprintf(compCIFfname, "%s/%s_comp%s_%d_%d_%d_%d.bin", resultDirectory, filename,format, QstepDC, QstepAC, intraPeriod,static_cast<int>(EC));
	#pragma pack(push, 1)
	FILE* fp = fopen(compCIFfname, "wb");
	if(fp==NULL)
	{
		cout << "fail to open "<< compCIFfname << endl;
		exit(-1);
	}
	fwrite(&hd, sizeof(header), 1, fp);
	#pragma pack(pop)

	// We have one coder for each as they will likely have different values
	evx::entropy_coder dcCoder {};
	evx::entropy_coder acCoder {};
	evx::entropy_coder mvCoder {};
	
	/* body */
	if(predmode==INTRA)
	{
		if (EC == EntropyCoding::Cabac) allintraBodyCabac(frames,nframes,QstepDC,fp,stats);
		else allintraBody(frames, nframes, fp, dcCoder, acCoder, stats);
	}
	else if(predmode==INTER)
	{
		int cntbits = 0;
		// Size of 8 bits (1 byte) for every pixel
		int maxbits = frames->splitHeight * frames->splitWidth * frames->blocks->blocksize1 * frames->blocks->blocksize1 * nframes * 8;
		if (nframes <= 480 && nframes > 300) maxbits *= 4;
		else if (nframes <= 300)maxbits *= 6;
		// if (EC == EntropyCoding::Cabac) maxbits /= nframes;
		// if (nframes > 480) maxbits4 /= ;
		// Allocate this number in bytes
		unsigned char* tempFrame = (unsigned char*)malloc(sizeof(unsigned char)*(maxbits/8));
		uint8_t* tempFrameEnd = tempFrame + (maxbits/8);
		if(tempFrame==NULL)
		{
			cout << "fail to allocate memory in makebitstream INTER" << endl;
			exit(-1);
		}

		for(int n=0; n<nframes; n++)
		{
			printf("starting frame: %d encoding type %s ..\n",n, n%intraPeriod==0 ? "I" : "P") ;
			x264_cabac_t cb;
			if (EC == EntropyCoding::Cabac){
				int slice_type = (n % intraPeriod == 0) ? SLICE_TYPE_I : SLICE_TYPE_P;
				x264_cabac_context_init(&cb, slice_type, QstepDC, 0);
        		x264_cabac_encode_init(&cb, tempFrame, tempFrameEnd);
			}
			if(n%intraPeriod==0)
			{
				if (EC == EntropyCoding::Cabac) intraBodyCabac(frames[n], tempFrame, cntbits, cb, stats);
				else intraBody(frames[n], tempFrame, cntbits, dcCoder, acCoder, stats);
				// cout << "intra frame bits: " << cntbits << endl;
			}
			else
			{
				if(EC == EntropyCoding::Cabac) interBodyCabac(frames[n], tempFrame, cntbits,cb, stats);
				else interBody(frames[n], tempFrame, cntbits, dcCoder, acCoder, mvCoder, stats);
				// cout << "inter frame bits: " << cntbits << endl;				
			}
			printf("frame: %d encoded..\n",n);
			if (EC == EntropyCoding::Cabac){
				// Terminate & flush CABAC
        		x264_cabac_encode_terminal(&cb);
        		x264_cabac_encode_flush(n, &cb);
        		// write out this frame's bitstream
        		size_t payload = cb.p - cb.p_start;
        		fwrite(cb.p_start, 1, payload, fp);
        		if (stats)
        		    stats->totalEntropyBits[frames[n].numOfFrame] = payload * 8;
			}
		}
		if (EC != EntropyCoding::Cabac)
			fwrite(tempFrame, (cntbits/8)+1, 1, fp);
		free(tempFrame);
	}
	
	fclose(fp);
}
void headerinit(header& hd, int height, int width, int QstepDC, int QstepAC, int intraPeriod)
{
	hd.intro[0] = 0; hd.intro[1] = 73; hd.intro[2] = 67; hd.intro[3] = 83; hd.intro[4] = 80;
	hd.height = height;
	hd.width  = width;
	hd.QP_DC = QstepDC;
	hd.QP_AC = QstepAC;
	hd.DPCMmode = 0;

	hd.outro = 0; // MV Pred = 0;
	for(int i=0; i<6; i++)
	{
		//(hd.outro<<=i) |= (intraPeriod>>(5-i))&1; // intraPeriod
		(hd.outro<<=1) |= (intraPeriod>>(5-i))&1; // intraPeriod		
	}

	(hd.outro <<= 1) |= 0;		// intraPrediction flag = 0;
	for(int i=0; i<6; i++)
	{
		(hd.outro <<= 1) |= 0;	// last bits = 0;
	}
}

void allintraBodyCabac(FrameData* frames, int nframes, int QstepDC, FILE* fp, Statistics *stats) {
    int totalblck = frames->nblocks16;
    int nblock8   = frames->nblocks8;
    int maxbytes  = frames->splitHeight * frames->splitWidth * frames->blocks->blocksize1 *
                    frames->blocks->blocksize1 * 8; // estimate in bytes
	if (nframes <= 480) maxbytes *= 2;
    // Allocate output buffer
    uint8_t* frame = (uint8_t*)malloc(sizeof(uint8_t) * maxbytes);
    if (frame == NULL) {
        std::cerr << "Failed to allocate memory for CABAC output.\n";
        exit(-1);
    }
    uint8_t* frameEnd = frame + maxbytes;

    for (int nfrm = 0; nfrm < nframes; nfrm++) {
        x264_cabac_t cb;
		memset(frame,0,maxbytes*sizeof(uint8_t));
        x264_cabac_context_init(&cb, SLICE_TYPE_I, QstepDC, 0);
        x264_cabac_encode_init(&cb, frame, frameEnd);

        for (int nblck16 = 0; nblck16 < totalblck; nblck16++) {
            BlockData& bd = frames[nfrm].blocks[nblck16];
            for (int nblck8 = 0; nblck8 < nblock8; nblck8++) {
                x264_cabac_encode_decision(&cb, CTX_MPM_FLAG, bd.MPMFlag[nblck8]);
                x264_cabac_encode_decision(&cb, CTX_INTRA_PRED, bd.intraPredMode[nblck8]);
				int pos0 = x264_cabac_pos(&cb);
                int dc = bd.intraReorderedblck8[nblck8][0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frames[nfrm].numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb); 
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, bd.intraACflag[nblck8]);

                if (bd.intraACflag[nblck8] == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = bd.intraReorderedblck8[nblck8][i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frames[nfrm].numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }

            // Cb/Cr processing
            CBlockData& cbbd = frames[nfrm].Cbblocks[nblck16];
            CBlockData& crbd = frames[nfrm].Crblocks[nblck16];

            for (int plane = 0; plane < 2; plane++) {
                CBlockData& cbd = (plane == 0) ? cbbd : crbd;

                int pos0 = x264_cabac_pos(&cb);
                int dc = cbd.intraReorderedblck[0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frames[nfrm].numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb);
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, cbd.intraACflag);

                if (cbd.intraACflag == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = cbd.intraReorderedblck[i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frames[nfrm].numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }
        }

        // Finalize CABAC encoding for this frame
        x264_cabac_encode_terminal(&cb);
        x264_cabac_encode_flush(nfrm, &cb);

        // Calculate size of encoded data
        size_t payload_size = cb.p - cb.p_start;
        fwrite(cb.p_start, 1, payload_size, fp);

        if (stats)
            stats->totalEntropyBits[frames[nfrm].numOfFrame] = payload_size * 8;
    }

    free(frame);
}

void allintraBody(FrameData* frames, int nframes, FILE* fp, evx::entropy_coder& dcCoder, evx::entropy_coder& acCoder, Statistics *stats)
{
	int totalblck = frames->nblocks16;
	int nblock8   = frames->nblocks8;
	int idx       = 0;
	int maxbits = frames->splitHeight * frames->splitWidth * frames->blocks->blocksize1 * frames->blocks->blocksize1 * nframes * 8;
	if (nframes <= 480 && nframes > 300) maxbits *= 4;
	else if (nframes <= 300)maxbits *= 6;
	
	int cntbits   = 0;
	int DCbits    = 0;
	int ACbits    = 0;
	int bytbits   = 0;

	unsigned char* frame = NULL;
	unsigned char* DCResult = NULL;
	unsigned char* ACResult = NULL;
	
	frame = (unsigned char*)malloc(sizeof(unsigned char)*(maxbits/8));
	if(frame==NULL)
	{
		cout << "fail to allocate memory in allintraBody" << endl;
		exit(-1);
	}
	for(int nfrm=0; nfrm<nframes; nfrm++)
	{		
		for(int nblck16=0; nblck16<totalblck; nblck16++)
		{
			BlockData& bd = frames[nfrm].blocks[nblck16];
			// Y 16x16 ����
			for(int nblck8=0; nblck8<nblock8; nblck8++)
			{				
				DCbits = 0;
				ACbits = 0;

				(frame[cntbits++/8]<<=1) |= bd.MPMFlag[nblck8]; // mpmflag 1bit				
				(frame[cntbits++/8]<<=1) |= bd.intraPredMode[nblck8]; // intra prediction flag 1bit				

				DCResult = DCentropy(bd.intraReorderedblck8[nblck8][0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
				// for(int n=0; n<DCbits; n++)
				// 	(frame[cntbits++/8]<<=1) |= DCResult[n];
				// free(DCResult);
				// DCResult = dcCabacEntropy(bd.intraReorderedblck8[nblck8][0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
				for(int n=0; n<DCbits; n++)
					(frame[cntbits++/8]<<=1) |= DCResult[n];
				free(DCResult);

				if (stats) {
					stats->totalDcBits[frames[nfrm].numOfFrame] += DCbits;
				}
								
				(frame[cntbits++/8]<<=1) |= bd.intraACflag[nblck8]; // acflag 1bit
				
				if(bd.intraACflag[nblck8]==1)
				{
					for(int n=0; n<63; n++)
						(frame[cntbits++/8]<<=1) |= 0;	

					if (stats)
						stats->totalAcBits[frames[nfrm].numOfFrame] += 63;
				}
				else
				{
					// ACResult = ACentropy(bd.intraReorderedblck8[nblck8], ACbits, stats); // AC entropy result ?bits; 63 values
					// for(int n=0; n<ACbits; n++)
					// 	(frame[cntbits++/8]<<=1) |= ACResult[n];
					// free(ACResult);
					ACResult = ACentropy(bd.intraReorderedblck8[nblck8], ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
					for(int n=0; n<ACbits; n++)
						(frame[cntbits++/8]<<=1) |= ACResult[n];
					free(ACResult);

					if (stats) {
						stats->totalAcBits[frames[nfrm].numOfFrame] += ACbits;
					}
				}
			}
			// Cb Cr 8x8 ����
			CBlockData& cbbd = frames[nfrm].Cbblocks[nblck16];
			CBlockData& crbd = frames[nfrm].Crblocks[nblck16];

			DCbits = 0;
			ACbits = 0;

			// DCResult = DCentropy(cbbd.intraReorderedblck[0], DCbits, stats); // DC entropy result ?bits; one value
			// for(int n=0; n<DCbits; n++)
			// 	(frame[cntbits++/8]<<=1) |= DCResult[n];
			// free(DCResult);
			DCResult = DCentropy(cbbd.intraReorderedblck[0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
			for(int n=0; n<DCbits; n++)
				(frame[cntbits++/8]<<=1) |= DCResult[n];
			free(DCResult);

			if (stats) {
				stats->totalDcBits[frames[nfrm].numOfFrame] += DCbits;
			}

			(frame[cntbits++/8]<<=1) |= cbbd.intraACflag; // acflag 1bit

			if(cbbd.intraACflag==1)
			{
				for(int n=0; n<63; n++)
					(frame[cntbits++/8]<<=1) |= 0;	

				if (stats)
					stats->totalAcBits[frames[nfrm].numOfFrame] += 63;
			}
			else
			{
				// ACResult = ACentropy(cbbd.intraReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
				// for(int n=0; n<ACbits; n++)
				// 	(frame[cntbits++/8]<<=1) |= ACResult[n];
				// free(ACResult);
				ACResult = ACentropy(cbbd.intraReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
				for(int n=0; n<ACbits; n++)
					(frame[cntbits++/8]<<=1) |= ACResult[n];
				free(ACResult);

				if (stats) {
					stats->totalAcBits[frames[nfrm].numOfFrame] += ACbits;
				}
			}
			DCbits = 0;
			ACbits = 0;

			// DCResult = DCentropy(crbd.intraReorderedblck[0], DCbits, stats); // DC entropy result ?bits; one value
			// for(int n=0; n<DCbits; n++)
			// 	(frame[cntbits++/8]<<=1) |= DCResult[n];
			// free(DCResult);
			DCResult = DCentropy(crbd.intraReorderedblck[0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
			for(int n=0; n<DCbits; n++)
				(frame[cntbits++/8]<<=1) |= DCResult[n];
			free(DCResult);

			if (stats) {
				stats->totalDcBits[frames[nfrm].numOfFrame] += DCbits;
			}

			(frame[cntbits++/8]<<=1) |= crbd.intraACflag; // acflag 1bit
			if(crbd.intraACflag==1)
			{
				for(int n=0; n<63; n++)
					(frame[cntbits++/8]<<=1) |= 0;	

				if (stats)
					stats->totalAcBits[frames[nfrm].numOfFrame] += 63;
			}
			else
			{
				// ACResult = ACentropy(crbd.intraReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
				// for(int n=0; n<ACbits; n++)
				// 	(frame[cntbits++/8]<<=1) |= ACResult[n];
				// free(ACResult);
				ACResult = ACentropy(crbd.intraReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
				for(int n=0; n<ACbits; n++)
					(frame[cntbits++/8]<<=1) |= ACResult[n];
				free(ACResult);

				if (stats) {
					stats->totalAcBits[frames[nfrm].numOfFrame] += ACbits;
				}
			}
		}	
		if (stats) {
			int i = frames[nfrm].numOfFrame;
			// We do not have MV bits here as it’s an intra frame
			stats->totalEntropyBits[i] = stats->totalAcBits[i] + stats->totalDcBits[i];
		}
	}
	dcCoder.clear();
	acCoder.clear();
	fwrite(frame, (cntbits/8)+1, 1, fp); 
	free(frame);
}

void intraBodyCabac(FrameData& frm, unsigned char* tempFrame, int& cntbits,x264_cabac_t& cb, Statistics *stats) {
	int totalblck = frm.nblocks16;
	int nblock8   = frm.nblocks8;
	int idx       = 0;
	int DCbits    = 0;
	int ACbits    = 0;
	int bytbits   = 0;

	unsigned char* DCResult = NULL;
	unsigned char* ACResult = NULL;
	for (int nblck16 = 0; nblck16 < totalblck; nblck16++) {
            BlockData& bd = frm.blocks[nblck16];
            for (int nblck8 = 0; nblck8 < nblock8; nblck8++) {
                x264_cabac_encode_decision(&cb, CTX_MPM_FLAG, bd.MPMFlag[nblck8]);
                x264_cabac_encode_decision(&cb, CTX_INTRA_PRED, bd.intraPredMode[nblck8]);
				int pos0 = x264_cabac_pos(&cb);
                int dc = bd.intraReorderedblck8[nblck8][0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb);
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, bd.intraACflag[nblck8]);
			
                if (bd.intraACflag[nblck8] == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = bd.intraReorderedblck8[nblck8][i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }

            // Cb/Cr processing
            CBlockData& cbbd = frm.Cbblocks[nblck16];
            CBlockData& crbd = frm.Crblocks[nblck16];

            for (int plane = 0; plane < 2; plane++) {
                CBlockData& cbd = (plane == 0) ? cbbd : crbd;

                int pos0 = x264_cabac_pos(&cb);
                int dc = cbd.intraReorderedblck[0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb);
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, cbd.intraACflag);

                if (cbd.intraACflag == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = cbd.intraReorderedblck[i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }
        }
}

void intraBody(FrameData& frm, unsigned char* tempFrame, int& cntbits, evx::entropy_coder& dcCoder, evx::entropy_coder& acCoder, Statistics *stats)
{
	int totalblck = frm.nblocks16;
	int nblock8   = frm.nblocks8;
	int idx       = 0;
	
	int DCbits    = 0;
	int ACbits    = 0;
	int bytbits   = 0;

	unsigned char* DCResult = NULL;
	unsigned char* ACResult = NULL;

	// Note: when addressing with `cntbits`, it is always divided by 8 because
	// we’re writing in a byte array (or unsigned char array) and so we’re
  // addressing the bytes, not the bits.
  //
  // By the nature of integer division, when one byte is fully written to,
  // the next is automatically addressed next time:
  //   frm[cntbits++/8] |= 0
	
	//cntbits = 0;
	//cout << "intra frame bits: " << cntbits << endl;
	for(int nblck16=0; nblck16<totalblck; nblck16++)
	{
		BlockData& bd = frm.blocks[nblck16];
		// Y 16x16 ����
		//cout << nblck16 << " blocks" << endl;
		for(int nblck8=0; nblck8<nblock8; nblck8++)
		{				
			DCbits = 0;
			ACbits = 0;

			(tempFrame[cntbits++/8]<<=1) |= bd.MPMFlag[nblck8];				 // mpmflag 1bit				
			(tempFrame[cntbits++/8]<<=1) |= bd.intraPredMode[nblck8];		 // intra prediction flag 1bit				

			// DCResult = DCentropy(bd.intraReorderedblck8[nblck8][0], DCbits, stats); // DC entropy result ?bits; one value
			// for(int n=0; n<DCbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
			// free(DCResult);
			DCResult = DCentropy(bd.intraReorderedblck8[nblck8][0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
			for(int n=0; n<DCbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
			free(DCResult);

			if (stats) {
				stats->totalDcBits[frm.numOfFrame] += DCbits;
			}

			(tempFrame[cntbits++/8]<<=1) |= bd.intraACflag[nblck8];		  // acflag 1bit
			if(bd.intraACflag[nblck8]==1)
			{
				for(int n=0; n<63; n++)
					(tempFrame[cntbits++/8]<<=1) |= 0;

				if (stats)
					stats->totalAcBits[frm.numOfFrame] += 63;
			}
			else
			{
				// ACResult = ACentropy(bd.intraReorderedblck8[nblck8], ACbits, stats); // AC entropy result ?bits; 63 values
				// for(int n=0; n<ACbits; n++)
				// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
				// free(ACResult);
				ACResult = ACentropy(bd.intraReorderedblck8[nblck8], ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
				for(int n=0; n<ACbits; n++)
					(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
				free(ACResult);

				if (stats) {
					stats->totalAcBits[frm.numOfFrame] += ACbits;
				}
			}

		}
		// Cb Cr 8x8 ����
		CBlockData& cbbd = frm.Cbblocks[nblck16];
		CBlockData& crbd = frm.Crblocks[nblck16];

		DCbits = 0;
		ACbits = 0;

		// DCResult = DCentropy(cbbd.intraReorderedblck[0], DCbits, stats);		 // DC entropy result ?bits; one value
		// for(int n=0; n<DCbits; n++)
		// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		// free(DCResult);
		DCResult = DCentropy(cbbd.intraReorderedblck[0], DCbits, dcCoder, stats);		 // DC entropy result ?bits; one value
		for(int n=0; n<DCbits; n++)
			(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		free(DCResult);

		if (stats) {
			stats->totalDcBits[frm.numOfFrame] += DCbits;
		}

		(tempFrame[cntbits++/8]<<=1) |= cbbd.intraACflag;	   // acflag 1bit
		if(cbbd.intraACflag==1)
		{
			for(int n=0; n<63; n++)
				(tempFrame[cntbits++/8]<<=1) |= 0;

			if (stats)
				stats->totalAcBits[frm.numOfFrame] += 63;
		}
		else
		{
			// ACResult = ACentropy(cbbd.intraReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
			// for(int n=0; n<ACbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			// free(ACResult);
			ACResult = ACentropy(cbbd.intraReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
			for(int n=0; n<ACbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			free(ACResult);

			if (stats) {
				stats->totalAcBits[frm.numOfFrame] += ACbits;
			}
		}

		DCbits = 0;
		ACbits = 0;

		// DCResult = DCentropy(crbd.intraReorderedblck[0], DCbits, stats); // DC entropy result ?bits; one value
		// for(int n=0; n<DCbits; n++)
		// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		// free(DCResult);
		DCResult = DCentropy(crbd.intraReorderedblck[0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
		for(int n=0; n<DCbits; n++)
			(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		free(DCResult);

		if (stats) {
			stats->totalDcBits[frm.numOfFrame] += DCbits;
		}

		(tempFrame[cntbits++/8]<<=1) |= crbd.intraACflag; // acflag 1bit
		if(crbd.intraACflag==1)
		{
			for(int n=0; n<63; n++)
				(tempFrame[cntbits++/8]<<=1) |= 0;

			if (stats)
				stats->totalAcBits[frm.numOfFrame] += 63;
		}
		else
		{
			// ACResult = ACentropy(crbd.intraReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
			// for(int n=0; n<ACbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			// free(ACResult);
			ACResult = ACentropy(crbd.intraReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
			for(int n=0; n<ACbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			free(ACResult);

			if (stats) {
				stats->totalAcBits[frm.numOfFrame] += ACbits;
			}
		}
	}
	if (stats) {
		int i = frm.numOfFrame;
		// We do not have MV bits here as it’s an intra frame
		stats->totalEntropyBits[i] = stats->totalAcBits[i] + stats->totalDcBits[i];
	}
	dcCoder.clear();
	acCoder.clear();
}

void interBodyCabac(FrameData& frm, unsigned char* tempFrame, int& cntbits,x264_cabac_t& cb, Statistics *stats) {
	int totalblck = frm.nblocks16;
	int nblock8   = frm.nblocks8;
	int idx       = 0;
	
	int DCbits    = 0;
	int ACbits    = 0;
	int bytbits   = 0;
	int xMVbits   = 0;
	int yMVbits   = 0;
	
	unsigned char* DCResult = NULL;
	unsigned char* ACResult = NULL;
	unsigned char* MVResult = NULL;

	if (stats) {
		stats->numberOfBlocks = totalblck;
		stats->mvPosX[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvPosY[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvDirX[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvDirY[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
	}
	for (int nblck16 = 0; nblck16 < totalblck; nblck16++) {
            BlockData& bd = frm.blocks[nblck16];
			if (stats) {
				stats->mvPosX[frm.numOfFrame][nblck16] = nblck16 % frm.splitWidth * 16 + 8;
				stats->mvPosY[frm.numOfFrame][nblck16] = nblck16 / frm.splitWidth * 16 + 8;
				stats->mvDirX[frm.numOfFrame][nblck16] = bd.mv.x;
				stats->mvDirY[frm.numOfFrame][nblck16] = bd.mv.y;
			}
			// has MV?
        	x264_cabac_encode_decision(&cb, CTX_MV_FLAG,  1);
			//X
			int pos0 = x264_cabac_pos(&cb);
			int mvx = bd.mv.x; 
        	x264_cabac_encode_ue_bypass(&cb, 0, abs(mvx));
        	x264_cabac_encode_bypass(&cb, mvx < 0);
			//Y
        	int mvy = bd.mv.y;
        	x264_cabac_encode_ue_bypass(&cb, 0, abs(mvy));
        	x264_cabac_encode_bypass   (&cb, mvy < 0);
        	if (stats) stats->totalMvBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;
            for (int nblck8 = 0; nblck8 < nblock8; nblck8++) {
                // x264_cabac_encode_decision(&cb, CTX_MPM_FLAG, bd.MPMFlag[nblck8]);
                // x264_cabac_encode_decision(&cb, CTX_INTRA_PRED, bd.intraPredMode[nblck8]);
				pos0 = x264_cabac_pos(&cb);
                int dc = bd.interReorderedblck8[nblck8][0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb);
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, bd.intraACflag[nblck8]);

                if (bd.intraACflag[nblck8] == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = bd.interReorderedblck8[nblck8][i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }

            // Cb/Cr processing
            CBlockData& cbbd = frm.Cbblocks[nblck16];
            CBlockData& crbd = frm.Crblocks[nblck16];

            for (int plane = 0; plane < 2; plane++) {
                CBlockData& cbd = (plane == 0) ? cbbd : crbd;

                int pos0 = x264_cabac_pos(&cb);
                int dc = cbd.interReorderedblck[0];
                x264_cabac_encode_decision(&cb, CTX_IDX_DC, dc != 0);
                if (dc) {
                    x264_cabac_encode_ue_bypass(&cb, 0, abs(dc));
                    x264_cabac_encode_bypass(&cb, dc < 0);
                }

                if (stats)
                    stats->totalDcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;

                pos0 = x264_cabac_pos(&cb);
                x264_cabac_encode_decision(&cb, CTX_AC_PRESENT, cbd.intraACflag);

                if (cbd.intraACflag == 1) {
                    for (int i = 0; i < 63; i++)
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, 0);
                } else {
                    for (int i = 0; i < 63; i++) {
                        int16_t c = cbd.interReorderedblck[i + 1];
                        int nz = (c != 0);
                        x264_cabac_encode_decision(&cb, CTX_IDX_AC_START + i, nz);
                        if (nz) {
                            x264_cabac_encode_ue_bypass(&cb, 0, abs(c));
                            x264_cabac_encode_bypass(&cb, c < 0);
                        }
                    }
                }

                if (stats)
                    stats->totalAcBits[frm.numOfFrame] += x264_cabac_pos(&cb) - pos0;
            }
        }
}

void interBody(FrameData& frm, unsigned char* tempFrame, int& cntbits, evx::entropy_coder& dcCoder, evx::entropy_coder& acCoder, evx::entropy_coder& mvCoder, Statistics *stats)
{
	int totalblck = frm.nblocks16;
	int nblock8   = frm.nblocks8;
	int idx       = 0;
	
	int DCbits    = 0;
	int ACbits    = 0;
	int bytbits   = 0;
	int xMVbits   = 0;
	int yMVbits   = 0;

	unsigned char* DCResult = NULL;
	unsigned char* ACResult = NULL;
	unsigned char* MVResult = NULL;

	if (stats) {
		stats->numberOfBlocks = totalblck;
		stats->mvPosX[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvPosY[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvDirX[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
		stats->mvDirY[frm.numOfFrame] = (int*) malloc(sizeof(int) * totalblck);
	}
	for(int nblck16=0; nblck16<totalblck; nblck16++)
	{
		BlockData& bd = frm.blocks[nblck16];

		if (stats) {
			stats->mvPosX[frm.numOfFrame][nblck16] = nblck16 % frm.splitWidth * 16 + 8;
			stats->mvPosY[frm.numOfFrame][nblck16] = nblck16 / frm.splitWidth * 16 + 8;
			stats->mvDirX[frm.numOfFrame][nblck16] = bd.mv.x;
			stats->mvDirY[frm.numOfFrame][nblck16] = bd.mv.y;
		}
		
		(tempFrame[cntbits++/8] <<= 1) |= 1;  // mv modeflag
		
		// MVResult = MVentropy(bd.mv, xMVbits, yMVbits, stats);  // mv; Reconstructedmv�� ���к��Ͱ� �������°� �ƴ� ���� ������ ���͸� ��������; �׷��� mv�� ����
		// for(int n=0; n<xMVbits+yMVbits; n++)
		// 		(tempFrame[cntbits++/8]<<=1) |= MVResult[n];
		// free(MVResult);
		MVResult = MVentropy(bd.mv, xMVbits, yMVbits, mvCoder, stats);  // mv; Reconstructedmv�� ���к��Ͱ� �������°� �ƴ� ���� ������ ���͸� ��������; �׷��� mv�� ����
		for(int n=0; n<xMVbits+yMVbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= MVResult[n];
		free(MVResult);
		if (stats)
			stats->totalMvBits[frm.numOfFrame] += xMVbits + yMVbits;

		// Y 16x16 ����
		for(int nblck8=0; nblck8<nblock8; nblck8++)
		{	
			// dc, ac
			DCbits = 0;
			ACbits = 0;

			// DCResult = DCentropy(bd.interReorderedblck8[nblck8][0], DCbits, stats); // DC entropy result ?bits; one value
			// for(int n=0; n<DCbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
			// free(DCResult);
			DCResult = DCentropy(bd.interReorderedblck8[nblck8][0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
			for(int n=0; n<DCbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
			free(DCResult);

			if (stats) {
				stats->totalDcBits[frm.numOfFrame] += DCbits;
			}
			(tempFrame[cntbits++/8]<<=1) |= bd.interACflag[nblck8]; // acflag 1bit
			if(bd.interACflag[nblck8] == 1)
			{
				for(int n=0; n<63; n++)
					(tempFrame[cntbits++/8]<<=1) |= 0;

				if (stats)
					stats->totalAcBits[frm.numOfFrame] += 63;
			}
			else
			{
				// ACResult = ACentropy(bd.interReorderedblck8[nblck8], ACbits, stats); // AC entropy result ?bits; 63 values
				// for(int n=0; n<ACbits; n++)
				// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
				// free(ACResult);
				ACResult = ACentropy(bd.interReorderedblck8[nblck8], ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
				for(int n=0; n<ACbits; n++)
					(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
				free(ACResult);

				if (stats) {
					stats->totalAcBits[frm.numOfFrame] += ACbits;
				}
			}
		}
		//cout << "Yframe bits: " << cntbits << endl;
		// Cb Cr 8x8 ����
		CBlockData& cbbd = frm.Cbblocks[nblck16];
		CBlockData& crbd = frm.Crblocks[nblck16];

		DCbits = 0;
		ACbits = 0;

		// DCResult = DCentropy(cbbd.interReorderedblck[0], DCbits, stats); // DC entropy result ?bits; one value
		// for(int n=0; n<DCbits; n++)
		// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		// free(DCResult);
		DCResult = DCentropy(cbbd.interReorderedblck[0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
		for(int n=0; n<DCbits; n++)
			(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		free(DCResult);

		if (stats) {
			stats->totalDcBits[frm.numOfFrame] += DCbits;
		}

		(tempFrame[cntbits++/8]<<=1) |= cbbd.interACflag; // acflag 1bit
		if(cbbd.interACflag == 1)
		{
			for(int n=0; n<63; n++)
				(tempFrame[cntbits++/8]<<=1) |= 0;

			if (stats)
				stats->totalAcBits[frm.numOfFrame] += 63;
		}
		else
		{
			// ACResult = ACentropy(cbbd.interReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
			// for(int n=0; n<ACbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			// free(ACResult);
			ACResult = ACentropy(cbbd.interReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
			for(int n=0; n<ACbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			free(ACResult);

			if (stats) {
				stats->totalAcBits[frm.numOfFrame] += ACbits;
			}
		}

		DCbits = 0;
		ACbits = 0;

		// DCResult = DCentropy(crbd.interReorderedblck[0], DCbits, stats); // DC entropy result ?bits; one value
		// for(int n=0; n<DCbits; n++)
		// 	(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		// free(DCResult);
		DCResult = DCentropy(crbd.interReorderedblck[0], DCbits, dcCoder, stats); // DC entropy result ?bits; one value
		for(int n=0; n<DCbits; n++)
			(tempFrame[cntbits++/8]<<=1) |= DCResult[n];
		free(DCResult);

		if (stats) {
			stats->totalDcBits[frm.numOfFrame] += DCbits;
		}

		(tempFrame[cntbits++/8]<<=1) |= crbd.interACflag; // acflag 1bit
		if(crbd.interACflag == 1)
		{
			for(int n=0; n<63; n++)
				(tempFrame[cntbits++/8]<<=1) |= 0;

			if (stats)
				stats->totalAcBits[frm.numOfFrame] += 63;
		}
		else
		{
			// ACResult = ACentropy(crbd.interReorderedblck, ACbits, stats); // AC entropy result ?bits; 63 values
			// for(int n=0; n<ACbits; n++)
			// 	(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			// free(ACResult);
			ACResult = ACentropy(crbd.interReorderedblck, ACbits, acCoder, stats); // AC entropy result ?bits; 63 values
			for(int n=0; n<ACbits; n++)
				(tempFrame[cntbits++/8]<<=1) |= ACResult[n];
			free(ACResult);

			if (stats) {
				stats->totalAcBits[frm.numOfFrame] += ACbits;
			}
		}
	}
	if (stats) {
		int i = frm.numOfFrame;
		// Here we have bits for all
		stats->totalEntropyBits[i] = stats->totalAcBits[i] + stats->totalDcBits[i] + stats->totalMvBits[i];
	}
	dcCoder.clear();
	acCoder.clear();
	mvCoder.clear();
}
int DCentropy(int DCval, unsigned char *DCentropyResult)
{
	int nbits = 0;
	int value = 0;
	int sign  = 0;
	int exp   = 0;
	int c     = 0;
	int idx   = 0;

	sign  = (DCval>=0)?1:0;
	value = abs(DCval);

	if(value==0)						 nbits=2;
	else if(value == 1)					 nbits=4;
	else if(value>=2    &&  value<=3)    nbits=5;
	else if(value>=4    &&  value<=7)    nbits=6;
	else if(value>=8    &&  value<=15)   nbits=7;
	else if(value>=16   &&  value<=31)   nbits=8;
	else if(value>=32   &&  value<=63)   nbits=10;
	else if(value>=64   &&  value<=127)  nbits=12;
	else if(value>=128  &&  value<=255)  nbits=14;
	else if(value>=256  &&  value<=511)  nbits=16;
	else if(value>=512  &&  value<=1023) nbits=18;
	else if(value>=1024 &&  value<=2047) nbits=20;
	else if(value>=2048)				 nbits=22;

	DCentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*nbits);	
	if(DCentropyResult==NULL)
	{
		cout << "fail to allocate DCentropyResult" << endl;
		exit(-1);
	}
	
	if(value == 0)					  
	{
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=0;
	}
	else if(value == 1)
	{
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;
	}
	else if(value>=2   &&  value<=3)
	{
		exp=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=sign;
		c=value-2;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;

	}
	else if(value>=4   &&  value<=7)
	{
		exp=2;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-4;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=8   &&  value<=15)
	{
		exp=3;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=sign;

		c=value-8;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=16  &&  value<=31)
	{
		exp=4;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-16;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=32  &&  value<=63)
	{
		exp=5;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-32;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=64  &&  value<=127)
	{
		exp=6;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-64;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=128 &&  value<=255)
	{
		exp=7;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-128;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=256 &&  value<=511)
	{
		exp=8;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-256;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=512 &&  value<=1023)
	{
		exp=9;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-512;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=1024 &&  value<=2047)
	{
		exp=10;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-1024;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=2048)
	{
		exp=11;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-2048;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}

	return nbits;
}
unsigned char* DCentropy(int DCval, int& nbits, evx::entropy_coder& encoder, Statistics* stats){
	if (EC == EntropyCoding::Abac) return DCentropyCabac(DCval,nbits,encoder, stats);
	return DCentropyOriginal(DCval,nbits, stats);
}
unsigned char* DCentropyOriginal(int DCval, int& nbits, Statistics* stats)
{
	// Absolute value
	int value = 0;
	// Sign, 0 if negative and 1 if positive
	int sign  = 0;
	int exp   = 0;
	int c     = 0;
	int idx   = 0;

	sign  = (DCval>=0)?1:0;
	value = abs(DCval);

	if(value==0)						 nbits=2;
	else if(value == 1)					 nbits=4;
	else if(value>=2    &&  value<=3)    nbits=5;
	else if(value>=4    &&  value<=7)    nbits=6;
	else if(value>=8    &&  value<=15)   nbits=7;
	else if(value>=16   &&  value<=31)   nbits=8;
	else if(value>=32   &&  value<=63)   nbits=10;
	else if(value>=64   &&  value<=127)  nbits=12;
	else if(value>=128  &&  value<=255)  nbits=14;
	else if(value>=256  &&  value<=511)  nbits=16;
	else if(value>=512  &&  value<=1023) nbits=18;
	else if(value>=1024 &&  value<=2047) nbits=20;
	else if(value>=2048)				 nbits=22;

	if (stats) {
		stats->dcNbitsHistogram[nbits] += 1;
		stats->dcValuesHistogram[min(value, 2048)] += 1;
	}

	// One unsigned char per bit
	unsigned char* DCentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*nbits);	
	
	if(DCentropyResult==NULL)
	{
		cout << "fail to allocate DCentropyResult" << endl;
		exit(-1);
	}
	
	if(value == 0)					  
	{
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=0;
	}
	else if(value == 1)
	{
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;
	}
	else if(value>=2   &&  value<=3)
	{
		exp=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=sign;
		c=value-2;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;

	}
	else if(value>=4   &&  value<=7)
	{
		exp=2;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-4;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=8   &&  value<=15)
	{
		exp=3;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=sign;

		c=value-8;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=16  &&  value<=31)
	{
		exp=4;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-16;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=32  &&  value<=63)
	{
		exp=5;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-32;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=64  &&  value<=127)
	{
		exp=6;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-64;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=128 &&  value<=255)
	{
		exp=7;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-128;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=256 &&  value<=511)
	{
		exp=8;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-256;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=512 &&  value<=1023)
	{
		exp=9;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-512;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=1024 &&  value<=2047)
	{
		exp=10;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-1024;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(value>=2048)
	{
		exp=11;
		for(int n=0; n<exp-2; n++)
			DCentropyResult[idx++]=1;
		DCentropyResult[idx++]=0;
		DCentropyResult[idx++]=sign;

		c=value-2048;
		for(int n=exp; n>0; n--)
			DCentropyResult[idx++] = (c>>(n-1))&1;
	}

	/*cout << "DC entropy" << endl;
	for(int i=0; i<idx; i++)
	{
		cout << (int)DCentropyResult[i] << " ";
	}
	cout << endl;*/
	return DCentropyResult;
}
int ACentropy(int* reordblck, unsigned char *ACentropyResult)
{
	int nbits  = 0;
	int value  = 0;
	int sign   = 0;
	int exp   = 0;
	int c     = 0;
	int idx   = 0;
	int length = 63; // except DC value of total 64 values

	for(int i=1; i<length; i++)
	{
		value = abs(reordblck[i+1]);
		if(value==0)						 nbits+=2;
		else if(value == 1)					 nbits+=4;
		else if(value>=2    &&  value<=3)    nbits+=5;
		else if(value>=4    &&  value<=7)    nbits+=6;
		else if(value>=8    &&  value<=15)   nbits+=7;
		else if(value>=16   &&  value<=31)   nbits+=8;
		else if(value>=32   &&  value<=63)   nbits+=10;
		else if(value>=64   &&  value<=127)  nbits+=12;
		else if(value>=128  &&  value<=255)  nbits+=14;
		else if(value>=256  &&  value<=511)  nbits+=16;
		else if(value>=512  &&  value<=1023) nbits+=18;
		else if(value>=1024 &&  value<=2047) nbits+=20;
		else if(value>=2048)				 nbits+=22;
	}


	ACentropyResult = (unsigned char*) malloc(sizeof(unsigned char)*nbits);	
	if(ACentropyResult==NULL)
	{
		cout << "fail to allocate ACentropyResult" << endl;
		exit(-1);
	}

	for(int i=1; i<length; i++)
	{
		sign  = (reordblck[i+1]>=0)?1:0;
		value = abs(reordblck[i+1]);
		if(value == 0)					  
		{
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=0;
		}
		else if(value == 1)
		{
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;
		}
		else if(value>=2   &&  value<=3)
		{
			exp=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=sign;
			c=value-2;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;

		}
		else if(value>=4   &&  value<=7)
		{
			exp=2;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-4;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=8   &&  value<=15)
		{
			exp=3;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=sign;

			c=value-8;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=16  &&  value<=31)
		{
			exp=4;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-16;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=32  &&  value<=63)
		{
			exp=5;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-32;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=64  &&  value<=127)
		{
			exp=6;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-64;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=128 &&  value<=255)
		{
			exp=7;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-128;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=256 &&  value<=511)
		{
			exp=8;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-256;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=512 &&  value<=1023)
		{
			exp=9;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-512;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=1024 &&  value<=2047)
		{
			exp=10;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-1024;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=2048)
		{
			exp=11;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-2048;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
	}

	return nbits;
}
unsigned char* ACentropyOriginal(int* reordblck, int& nbits,Statistics* stats ){
	int value  = 0;
	int sign   = 0;
	int exp    = 0;
	int c      = 0;
	int idx    = 0;
	int length = 63; // except DC value of total 64 values

	int lastNbits = 0;

	for(int i=0; i<length; i++)
	{
		value = abs(reordblck[i+1]);
		if(value==0)						 nbits+=2;
		else if(value == 1)					 nbits+=4;
		else if(value>=2    &&  value<=3)    nbits+=5;
		else if(value>=4    &&  value<=7)    nbits+=6;
		else if(value>=8    &&  value<=15)   nbits+=7;
		else if(value>=16   &&  value<=31)   nbits+=8;
		else if(value>=32   &&  value<=63)   nbits+=10;
		else if(value>=64   &&  value<=127)  nbits+=12;
		else if(value>=128  &&  value<=255)  nbits+=14;
		else if(value>=256  &&  value<=511)  nbits+=16;
		else if(value>=512  &&  value<=1023) nbits+=18;
		else if(value>=1024 &&  value<=2047) nbits+=20;
		else if(value>=2048)				 nbits+=22;

		if (stats) {
			stats->acNbitsHistogram[nbits-lastNbits] += 1;
			stats->acValuesHistogram[min(value, 2048)] += 1;
			lastNbits = nbits;
		}
	}


	unsigned char* ACentropyResult = (unsigned char*) malloc(sizeof(unsigned char)*nbits);	
	if(ACentropyResult==NULL)
	{
		cout << "fail to allocate ACentropyResult" << endl;
		exit(-1);
	}

	//cout << "AC entropy" << endl;
	for(int i=0; i<length; i++)
	{
		sign  = (reordblck[i+1]>=0)?1:0;
		value = abs(reordblck[i+1]);

		//////////
		int previdx = idx;
		//////////

		if(value == 0)					  
		{
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=0;
		}
		else if(value == 1)
		{
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;
		}
		else if(value>=2   &&  value<=3)
		{
			exp=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=sign;
			c=value-2;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;

		}
		else if(value>=4   &&  value<=7)
		{
			exp=2;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-4;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=8   &&  value<=15)
		{
			exp=3;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=sign;

			c=value-8;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=16  &&  value<=31)
		{
			exp=4;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-16;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=32  &&  value<=63)
		{
			exp=5;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-32;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=64  &&  value<=127)
		{
			exp=6;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-64;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=128 &&  value<=255)
		{
			exp=7;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-128;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=256 &&  value<=511)
		{
			exp=8;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-256;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=512 &&  value<=1023)
		{
			exp=9;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-512;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=1024 &&  value<=2047)
		{
			exp=10;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-1024;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}
		else if(value>=2048)
		{
			exp=11;
			for(int n=0; n<exp-2; n++)
				ACentropyResult[idx++]=1;
			ACentropyResult[idx++]=0;
			ACentropyResult[idx++]=sign;

			c=value-2048;
			for(int n=exp; n>0; n--)
				ACentropyResult[idx++] = (c>>(n-1))&1;
		}

		/*for(int nb=previdx; nb<idx; nb++)
		{
			cout << (int)ACentropyResult[nb] << " ";
		}
		cout << endl;*/
	}
	//system("pause");
	return ACentropyResult;
}

unsigned char* ACentropy(int* reordblck, int& nbits, evx::entropy_coder& encoder, Statistics* stats)
{
	if (EC == EntropyCoding::Abac) return ACentropyCabac(reordblck, nbits, encoder, stats);
	else if (EC == EntropyCoding::Huffman)  return ACentropyHuffman(reordblck, nbits, stats);
	return ACentropyOriginal(reordblck, nbits,stats);
	
}

unsigned char* ACentropyHuffman(int* reordblck, int& nbits, Statistics* stats) {
	const int length = 63;
	unordered_map<int, int> freq;

	// Step 1: Count frequencies of absolute values (skip DC)
	for (int i = 0; i < length; ++i) {
		int absval = abs(reordblck[i + 1]);
		freq[absval]++;
	}

	// Step 2: Build Huffman tree
	priority_queue<HuffNode*, vector<HuffNode*>, CompareNode> pq;
	for (const auto& entry : freq) {
		pq.push(new HuffNode(entry.first, entry.second));
	}
	while (pq.size() > 1) {
		HuffNode* left = pq.top(); pq.pop();
		HuffNode* right = pq.top(); pq.pop();
		pq.push(new HuffNode(left, right));
	}
	HuffNode* root = pq.top();

	// Step 3: Generate Huffman codes
	unordered_map<int, string> huffmanCode;
	buildCodes(root, "", huffmanCode);

	// Step 4: Encode bitstream
	vector<bool> bitstream;
	for (int i = 0; i < length; ++i) {
		int coeff = reordblck[i + 1];
		int absval = abs(coeff);
		int sign = coeff >= 0 ? 1 : 0;

		// Get Huffman code
		string code = huffmanCode[absval];
		for (char c : code)
			bitstream.push_back(c == '1');

		if (absval != 0)
			bitstream.push_back(sign);

		// Record statistics for histograms
		if (stats) {
			int bitsize = absval == 0 ? code.length() : code.length() + 1;
			stats->acNbitsHistogram[bitsize] += 1;
			stats->acValuesHistogram[min(absval, 2048)] += 1;
		}
	}

	nbits = bitstream.size();
	int nbytes = (nbits + 7) / 8;
	unsigned char* encoded = (unsigned char*)malloc(nbytes);
	if (!encoded) {
		cerr << "Memory allocation failed.\n";
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < nbytes; ++i) encoded[i] = 0;

	// Step 5: Pack bits into bytes
	for (int i = 0; i < nbits; ++i) {
		if (bitstream[i]) {
			encoded[i / 8] |= (1 << (7 - (i % 8)));
		}
	}

	// Clean up
	freeTree(root);

	return encoded;
}

unsigned char* MVentropyOriginal(MotionVector mv, int& nbitsx, int& nbitsy, Statistics* stats){
	int xValue = 0;
	int yValue = 0;
	int xsign  = 0;
	int ysign  = 0;
	int exp    = 0;
	int c      = 0;
	int idx    = 0;

	xsign  = (mv.x>=0)?1:0;
	xValue = abs(mv.x);

	if(xValue==0)						   nbitsx=2;
	else if(xValue==1)					   nbitsx=4;
	else if(xValue>=2    &&  xValue<=3)    nbitsx=5;
	else if(xValue>=4    &&  xValue<=7)    nbitsx=6;
	else if(xValue>=8    &&  xValue<=15)   nbitsx=7;
	else if(xValue>=16   &&  xValue<=31)   nbitsx=8;
	else if(xValue>=32   &&  xValue<=63)   nbitsx=10;
	else if(xValue>=64   &&  xValue<=127)  nbitsx=12;
	else if(xValue>=128  &&  xValue<=255)  nbitsx=14;
	else if(xValue>=256  &&  xValue<=511)  nbitsx=16;
	else if(xValue>=512  &&  xValue<=1023) nbitsx=18;
	else if(xValue>=1024 &&  xValue<=2047) nbitsx=20;
	else if(xValue>=2048)				   nbitsx=22;	

	ysign  = (mv.y>=0)?1:0;
	yValue = abs(mv.y);

	if(yValue==0)						   nbitsy=2;
	else if(yValue==1)					   nbitsy=4;
	else if(yValue>=2    &&  yValue<=3)    nbitsy=5;
	else if(yValue>=4    &&  yValue<=7)    nbitsy=6;
	else if(yValue>=8    &&  yValue<=15)   nbitsy=7;
	else if(yValue>=16   &&  yValue<=31)   nbitsy=8;
	else if(yValue>=32   &&  yValue<=63)   nbitsy=10;
	else if(yValue>=64   &&  yValue<=127)  nbitsy=12;
	else if(yValue>=128  &&  yValue<=255)  nbitsy=14;
	else if(yValue>=256  &&  yValue<=511)  nbitsy=16;
	else if(yValue>=512  &&  yValue<=1023) nbitsy=18;
	else if(yValue>=1024 &&  yValue<=2047) nbitsy=20;
	else if(yValue>=2048)				   nbitsy=22;	
	
	if (stats) {
		stats->mvxNbitsHistogram[nbitsx] += 1;
		stats->mvyNbitsHistogram[nbitsy] += 1;
		stats->mvxValuesHistogram[min(xValue, 2048)] += 1;
		stats->mvyValuesHistogram[min(yValue, 2048)] += 1;
	}
	
	unsigned char* MVentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*(nbitsx+nbitsy));
	if(MVentropyResult==NULL)
	{
		cout << "fail to allocate MVentropyResult" << endl;
		exit(-1);
	}
	
	if(xValue == 0)					  
	{
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=0;
	}
	else if(xValue == 1)
	{
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;
	}
	else if(xValue>=2   &&  xValue<=3)
	{
		exp=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=xsign;
		c=xValue-2;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;

	}
	else if(xValue>=4   &&  xValue<=7)
	{
		exp=2;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-4;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=8   &&  xValue<=15)
	{
		exp=3;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=xsign;

		c=xValue-8;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=16  &&  xValue<=31)
	{
		exp=4;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-16;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=32  &&  xValue<=63)
	{
		exp=5;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-32;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=64  &&  xValue<=127)
	{
		exp=6;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-64;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=128 &&  xValue<=255)
	{
		exp=7;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-128;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=256 &&  xValue<=511)
	{
		exp=8;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-256;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=512 &&  xValue<=1023)
	{
		exp=9;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-512;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=1024 &&  xValue<=2047)
	{
		exp=10;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-1024;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(xValue>=2048)
	{
		exp=11;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=xsign;

		c=xValue-2048;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	


	if(yValue == 0)					  
	{
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=0;
	}
	else if(yValue == 1)
	{
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;
	}
	else if(yValue>=2   &&  yValue<=3)
	{
		exp=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=ysign;
		c=yValue-2;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;

	}
	else if(yValue>=4   &&  yValue<=7)
	{
		exp=2;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-4;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=8   &&  yValue<=15)
	{
		exp=3;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=ysign;

		c=yValue-8;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=16  &&  yValue<=31)
	{
		exp=4;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-16;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=32  &&  yValue<=63)
	{
		exp=5;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-32;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=64  &&  yValue<=127)
	{
		exp=6;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-64;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=128 &&  yValue<=255)
	{
		exp=7;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-128;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=256 &&  yValue<=511)
	{
		exp=8;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-256;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=512 &&  yValue<=1023)
	{
		exp=9;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-512;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=1024 &&  yValue<=2047)
	{
		exp=10;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-1024;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}
	else if(yValue>=2048)
	{
		exp=11;
		for(int n=0; n<exp-2; n++)
			MVentropyResult[idx++]=1;
		MVentropyResult[idx++]=0;
		MVentropyResult[idx++]=ysign;

		c=yValue-2048;
		for(int n=exp; n>0; n--)
			MVentropyResult[idx++] = (c>>(n-1))&1;
	}

	return MVentropyResult;
}
unsigned char* MVentropy(MotionVector mv, int& nbitsx, int& nbitsy,evx::entropy_coder& encoder, Statistics* stats){
	if(EC == EntropyCoding::Abac) return MVentropyCabac(mv,nbitsx,nbitsy, encoder, stats);
	return MVentropyOriginal(mv,nbitsx,nbitsy, stats);
}

unsigned char* DCentropyCabac(int DCval, int& nbits, evx::entropy_coder& encoder, Statistics* stats) {
	auto src = evx::bitstream {(void *) &DCval, sizeof(int)};
	auto dst = evx::bitstream {(sizeof(int) << 3)};
	encoder.encode(&src, &dst, false);
	nbits = dst.query_occupancy();
	auto DCentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*nbits);	
	dst.seek(0);
	for (int i = 0; i < nbits; i++)
		dst.read_bit(DCentropyResult + i);
	return DCentropyResult;
}

unsigned char* ACentropyCabac(int* reordblck, int& nbits, evx::entropy_coder& encoder, Statistics* stats) {
	// Size of 63 ints, as much as the AC values
	constexpr unsigned len = sizeof(int) * 63;
	auto src = evx::bitstream {(void *) (reordblck + 1), len};
	auto dst = evx::bitstream {len};
	encoder.encode(&src, &dst, false);
	nbits = dst.query_occupancy();
	auto ACentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*nbits);	
	dst.seek(0);
	for (int i = 0; i < nbits; i++)
		dst.read_bit(ACentropyResult + i);
	return ACentropyResult;
}

unsigned char* MVentropyCabac(MotionVector mv, int& nbitsx, int& nbitsy, evx::entropy_coder& encoder, Statistics* stats) {
	auto src_x = evx::bitstream {(void *) &mv.x, sizeof(int)};
	auto src_y = evx::bitstream {(void *) &mv.y, sizeof(int)};
	auto dst = evx::bitstream {sizeof(int) * 8 * 2};
	encoder.encode(&src_x, &dst, false);
	nbitsx = dst.query_occupancy();
	encoder.encode(&src_y, &dst, false);
	nbitsy = dst.query_occupancy() - nbitsx;
	auto MVentropyResult = (unsigned char*)malloc(sizeof(unsigned char)*(nbitsx+nbitsy));
	dst.seek(0);
	for (int i = 0; i < nbitsx + nbitsy; i++)
		dst.read_bit(MVentropyResult + i);
	return MVentropyResult;
}

/* checking image function */
void checkResultY(unsigned char *Y, int width, int height)
{
	FILE* output_fp;
	char CIF_path[256] = "..\\CIF(352x288)";
	char output_name[256] = "check_test_gray.yuv";
	char output_fname[256];

	sprintf(output_fname, "%s\\%s", CIF_path, output_name);

	output_fp = fopen(output_fname, "wb");
	if(output_fp==NULL)
	{
		cout << "fail to save yuv" << endl;
		return;
	}

	fwrite(Y, sizeof(unsigned char)*height*width, 1, output_fp);
	fclose(output_fp);
}
void checkResultYUV(unsigned char *Y, unsigned char *Cb, unsigned char *Cr, int width, int height)
{
	FILE* output_fp;
	char CIF_path[256] = "..\\CIF(352x288)";
	char output_name[256] = "check_test_YUV.yuv";
	char output_fname[256];
	sprintf(output_fname, "%s\\%s", CIF_path, output_name);

	output_fp = fopen(output_fname, "wb");	
	if(output_fp==NULL)
	{
		cout << "fail to save yuv" << endl;
		return;
	}
	fwrite(Y,  sizeof(unsigned char)*height*width, 1, output_fp);
	fwrite(Cb, sizeof(unsigned char)*(height/2)*(width/2), 1, output_fp);
	fwrite(Cr, sizeof(unsigned char)*(height/2)*(width/2), 1, output_fp);
	fclose(output_fp);
}
void checkResultFrames(FrameData* frm,char* fname, int width, int height, int nframe,int QstepDC, int QstepAC, int intraPeriod, int predtype, int chtype)
{
	FILE* output_fp, *output_fp_error_image;

	char output_fname[256];
	sprintf(output_fname, "%s_%d_%d_%d_decoded.yuv" ,fname,QstepDC,QstepAC,intraPeriod);

	output_fp = fopen(output_fname, "wb");
	if(output_fp==NULL)
	{
		cout << "fail to save yuv" << endl;
		return;
	}

	sprintf(output_fname, "%s_%d_%d_%d_errors.yuv" ,fname,QstepDC,QstepAC,intraPeriod);

	output_fp_error_image = fopen(output_fname, "wb");
	if(output_fp==NULL)
	{
		cout << "fail to save yuv" << endl;
		return;
	}

	int yFrameSize = height*width;
	int yFrameBytes = sizeof(unsigned char)*yFrameSize;
	int uvFrameSize = (height/2)*(width/2);
	int uvFrameBytes = sizeof(unsigned char)*uvFrameSize;

	auto errorY = (unsigned char**) malloc(nframe*sizeof(unsigned char**));
	auto errorU = (unsigned char**) malloc(nframe*sizeof(unsigned char**));
	auto errorV = (unsigned char**) malloc(nframe*sizeof(unsigned char**));
	for (int i = 0; i < nframe; i++) {
		errorY[i] = (unsigned char*) malloc(yFrameBytes);
		errorU[i] = (unsigned char*) malloc(uvFrameBytes);
		errorV[i] = (unsigned char*) malloc(uvFrameBytes);
	}

	// First frame does not have an error image
	memset(errorY[0], 0, yFrameBytes);
	memset(errorU[0], 128, uvFrameBytes);
	memset(errorV[0], 128, uvFrameBytes);

	// Compute the error frames
	for (int i = 1; i < nframe; i++) {
		// Y
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const int idx = y*width + x;
				// We convert to int and back to unsigned char to avoid overflows
				unsigned char errorYVal = (unsigned char) abs((int)frm[i].reconstructedY[idx] - (int)frm[i].Y[idx]);
				errorY[i][idx] = errorYVal;
			}
		}
		// UV
		for (int y = 0; y < height / 2; y++) {
			for (int x = 0; x < width / 2; x++) {
				const int idx = y*width/2 + x;
				// We convert to int and back to unsigned char to avoid overflows
				unsigned char errorUVal = (unsigned char) abs((int)frm[i].reconstructedCb[idx] - (int)frm[i].Cb[idx]);
				unsigned char errorVVal = (unsigned char) abs((int)frm[i].reconstructedCr[idx] - (int)frm[i].Cr[idx]);
				errorU[i][idx] = 128;
				errorV[i][idx] = 128;
			}
		}
	}

	if(chtype==SAVE_Y)	// Y�θ� �� ���� �����
	{
		for(int i=0; i<nframe; i++)
		{
			// Reconstructed YUV
			fwrite(frm[i].reconstructedY, yFrameBytes, 1, output_fp);
			// Difference YUV
			fwrite(errorY[i], yFrameBytes, 1, output_fp_error_image);
		}
	}
	else if(chtype==SAVE_YUV)
	{
		for(int i=0; i<nframe; i++)
		{

			// Reconstructed YUV
			fwrite(frm[i].reconstructedY,  yFrameBytes, 1, output_fp);
			fwrite(frm[i].reconstructedCb,  uvFrameBytes, 1, output_fp);
			fwrite(frm[i].reconstructedCr,  uvFrameBytes, 1, output_fp);
			// Difference YUV
			fwrite(errorY[i],  yFrameBytes, 1, output_fp_error_image);
			fwrite(errorU[i],  uvFrameBytes, 1, output_fp_error_image);
			fwrite(errorV[i],  uvFrameBytes, 1, output_fp_error_image);
		}
	}

	fclose(output_fp);
	fclose(output_fp_error_image);

	for(int i=0; i<nframe; i++)
	{
		free(frm[i].reconstructedY);
		free(frm[i].reconstructedCb);
		free(frm[i].reconstructedCr);
		free(errorY[i]);
		free(errorU[i]);
		free(errorV[i]);
	}
}
void checkResultRestructedFrames(FrameData* frm, int width, int height, int nframe, int type)
{
	FILE* output_fp;
	char CIF_path[256] = "..\\CIF(352x288)";
	char output_name[256] = "test_restructed_frames.yuv";
	char output_fname[256];
	sprintf(output_fname, "%s\\%s", CIF_path, output_name);

	output_fp = fopen(output_fname, "wb");
	if(output_fp==NULL)
	{
		cout << "fail to save yuv" << endl;
		return;
	}

	int totalblck = frm->nblocks16;
	int nblck8 = frm->nblocks8;
	int splitWidth = frm->splitWidth;
	int splitHeight = frm->splitHeight;
	int blocksize1 = frm->blocks->blocksize1;
	int blocksize2 = frm->blocks->blocksize2;

	unsigned char* img = (unsigned char*) calloc(width * height, sizeof(unsigned char));

	free(img);
	fclose(output_fp);
}
void checkRestructed(FrameData* frms, int nframes, int width, int height, int predtype, int chtype) // type: 1 -> Y, 3 -> YCbCr
{
	FILE* fp;
	char CIF_path[256] = "..\\CIF(352x288)";
	char output_name[256];
	if (chtype == 1) sprintf(output_name, "Test_Restructed_Ychannel.yuv");
	else if (chtype == 3) sprintf(output_name, "Test_Restructed_YCbCr.yuv");

	char output_fname[256];
	sprintf(output_fname, "%s\\%s", CIF_path, output_name);
	fp = fopen(output_fname, "wb");

	int blocksize1 = frms->blocks->blocksize1;
	int blocksize2 = frms->blocks->blocksize2;
	int totalblck = frms->nblocks16;
	int nblck8 = frms->nblocks8;
	int splitWidth = frms->splitWidth;
	int splitHeight = frms->splitHeight;
	int CbCrSplitWidth = frms->CbCrSplitWidth;
	int CbCrSplitHeight = frms->CbCrSplitHeight;
	int CbCrWidth = frms->CbCrWidth;
	int CbCrHeight = frms->CbCrHeight;


	unsigned char* Ychannel = (unsigned char *)calloc(width * height, sizeof(unsigned char));
	unsigned char* Cbchannel = (unsigned char *)calloc((width / 2) * (height / 2), sizeof(unsigned char));
	unsigned char* Crchannel = (unsigned char *)calloc((width / 2) * (height / 2), sizeof(unsigned char));

	int temp = 0;
	int tempCb = 0;
	int tempCr = 0;
	int nblck = 0;
	if (chtype == 1)
	{
		for (int nfrm = 0; nfrm < nframes; nfrm++)
		{
			FrameData& frm = frms[nfrm]; // frame
			nblck = 0;
			for (int y_interval = 0; y_interval < splitHeight; y_interval++)
			{
				for (int x_interval = 0; x_interval < splitWidth; x_interval++)
				{
					BlockData &bd = frm.blocks[nblck]; // nblck++

					for (int y = 0; y < blocksize1; y++)
					{
						for (int x = 0; x < blocksize1; x++)
						{
							Ychannel[(y_interval*blocksize1*width) + (y*width) + (x_interval*blocksize1) + x] = bd.intraRestructedblck16.block[y][x];
						}
					}
					nblck++;
				}
			}
			fwrite(Ychannel, width*height, sizeof(unsigned char), fp);
		}
	}
	else if (chtype == 3)
	{
		for (int nfrm = 0; nfrm < nframes; nfrm++)
		{
			FrameData& frm = frms[nfrm]; // frame
			nblck = 0;
			for (int y_interval = 0; y_interval < splitHeight; y_interval++)
			{
				for (int x_interval = 0; x_interval < splitWidth; x_interval++)
				{
					BlockData &bd = frm.blocks[nblck]; // nblck++

					for (int y = 0; y < blocksize1; y++)
					{
						for (int x = 0; x < blocksize1; x++)
						{
							Ychannel[(y_interval*blocksize1*width) + (y*width) + (x_interval*blocksize1) + x] = bd.intraRestructedblck16.block[y][x];
						}
					}
					nblck++;
				}
			}
			nblck = 0;
			for (int y_interval = 0; y_interval < CbCrSplitHeight; y_interval++)
			{
				for (int x_interval = 0; x_interval < CbCrSplitWidth; x_interval++)
				{
					CBlockData &cbbd = frm.Cbblocks[nblck]; // nblck++
					CBlockData &crbd = frm.Crblocks[nblck];
					int index = 0;
					for (int y = 0; y < blocksize2; y++)
					{
						for (int x = 0; x < blocksize2; x++)
						{
							tempCb = (cbbd.intraInverseDCTblck->block[y][x] > 255) ? 255 : cbbd.intraInverseDCTblck->block[y][x];
							tempCb = (tempCb < 0) ? 0 : tempCb;
							index = (y_interval*blocksize2*CbCrWidth) + (y*CbCrWidth) + (x_interval*blocksize2) + x;
							Cbchannel[index] = tempCb;

							tempCr = (crbd.intraInverseDCTblck->block[y][x] > 255) ? 255 : crbd.intraInverseDCTblck->block[y][x];
							tempCr = (tempCr < 0) ? 0 : tempCr;
							Crchannel[index] = tempCr;
						}
					}
					nblck++;
				}
			}
			fwrite(Ychannel, width*height, sizeof(unsigned char), fp);
			fwrite(Cbchannel, sizeof(unsigned char), (width / 2)*(height / 2), fp);
			fwrite(Crchannel, sizeof(unsigned char), (width / 2)*(height / 2), fp);
		}
	}

	free(Ychannel);
	free(Cbchannel);
	free(Crchannel);

	fclose(fp);
}
