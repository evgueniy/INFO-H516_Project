#include "ICSP_Codec_Encoder.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

extern char filename[256];
extern char resultDirectory[];
int main(int argc, char *argv[])
{	
	cmd_options_t options;	
	set_command_options(argc, argv, &options);
	// split sequence name for saving bitstream output
	char *yuv_fname = options.yuv_fname;
	char* ptr = yuv_fname;
	char* start = yuv_fname;
	int size = 0;
	// Get to end of filename
	while (*ptr != 0) {
		ptr++;
	}

	// Get back to last separator
	while (*ptr != '/')
		ptr--;

	ptr++;
	start = ptr;

	// Get to end of yuv file name
	while (*ptr++ != '_')
		size++;
	
	memcpy(filename, start, size);
	filename[size] = 0;

	// Create directory if nonexistent
	struct stat st = {0};
	if (stat(resultDirectory, &st) == -1) {
    mkdir(resultDirectory, 0700);
	}
	printf("Resolution: %dx%d\n", options.width,options.height);
	Statistics stats {options.total_frames};

	IcspCodec icspCodec;
	icspCodec.init(options.total_frames, options.yuv_fname, options.width, options.height, options.QP_DC, options.QP_AC);
	icspCodec.encoding(&options, &stats);
	writeFrameStats(stats, filename, options.intra_period, options.QP_DC, options.QP_AC);
	writeHistogramBitsizeStats(stats, filename, options.intra_period, options.QP_DC, options.QP_AC);
	writeHistogramValueStats(stats, filename, options.intra_period, options.QP_DC, options.QP_AC);
	
	return 0;
}
