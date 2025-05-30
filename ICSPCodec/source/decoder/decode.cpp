#define IS_DECODER
#include "ICSP_Codec_Decoder.h"


int main(int argc, char *argv[])
{
	int nframes = atoi(argv[1]);
	char* binfname = argv[2]; 
	char* decoder = argv[3];
	char imgfname[256];
	sprintf(imgfname, argv[4]);

	/*int nframes = 300;
	char* binfname = "foreman_cif(352X288)_300f_compCIF_16_16_2.bin";
	int QPDC = 16;
	int QPAC = 16;
	int intraPeriod = 2;
	char imgfname[256];
	sprintf(imgfname, "foreman_cif(352X288)_300f.yuv");*/


	IcspCodec icspCodec;
	icspCodec.init(binfname, nframes, decoder);
	icspCodec.decoding(imgfname);
	return 0;
}

//akiyo_cif(352X288)_300f.yuv
//foreman_cif(352X288)_300f.yuv
//football_cif(352X288)_90f.yuv