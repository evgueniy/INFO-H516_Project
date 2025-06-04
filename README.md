
# Improving MPEG-1 using modern techniques

This repository holds the end-of-year project for the course *Visual Media
Compression* (INFO-H516) given by Prof. Gauthier Lafruit during the academic
year of 2025 at the *Université Libre de Bruxelles* (ULB).

The goal of the project is to put students in the shoes of MPEG consortium
members. The primary task is to achieve compression improvements over the
basic version of MPEG-1, but it also includes analysis of the results and
regular meetings held in rougly the same vein as the real ones.

## Requirements

For the codec no additional requirements are needed besides a C++ compiler
and `glibc`.

For the plotting scripts, `python3` is used along with the following libraries (a
`requirements.txt` is also available):

- [pandas](https://pandas.pydata.org/)
- [bjontegaard](https://github.com/FAU-LMS/bjontegaard)
- [opencv2](https://github.com/opencv/opencv-python)

And also `ffmpeg` for the scalable pipeline and the motion vectors.

## Compiling & Running

To setup the codec:

```bash
cd  ./ICSPCodec/build/Debug
./build.sh
```

To compile it:

```bash
cd  ./ICSPCodec/build/Debug
make
```

To run it, see `--help` for information on the parameters.

```text
./ICSPCodec [option] [values]
-i : input yuv sequence
-n : the number of frames (default is 1)
-q : QP of DC and AC
-h : set the height value (default CIF: 288)
-w : set the width value (default CIF: 352)
-e : set the entropy coder to use (original, abac, huffman or cabac - Default: original )
--help : help message
--qpdc : QP of DC
--qpac : QP of AC
--intraPeriod: period of intra frame(0: All intra)
```

One potential invocation could be:

```bash
cd  ./ICSPCodec/build/Debug
./ICSPCodec  -i  "../../data/table_cif(352X288)_300f.yuv"  -n  300  -q  16  --intraPeriod  8  --EnMultiThread  0
```

The entropy coder used can be changed using the `-e` paramater with one of the following values:

- `original` (default, used if parameter omitted)
- `abac`
- `huffman`
- `cabac`

Running it produces the following files found under `./results` (the example
file names are resulting from the above invocation):

- **Encoded Binary File**:  `table_compCIF_16_16_8_0.bin`
- **Decoded YUV File**:  `table_cif(352X288)_300f.yuv_16_16_8_decoded.yuv` (not in `./results`)
- **Error Image File**: `table_cif(352X288)_300f.yuv_16_16_8_errors.yuv` (not in `./results`)
- **Statistics File**: `table_16_16_8_0.csv`
- **Histogram by Value**: `hist_value_table_16_16_8_0.csv`
- **Histogram by Bitsize**: `hist_bitsize_table_16_16_8_0.csv`

The values at the end of the filenames are (as an example with the invocation above):

- 16 (quantization factor for DC)
- 16 (quantization factor for AC)
- 8 (intra frame period)
- 0 (encoder used: original=0, cabac=1, huffman=2)

## Scripts

There are several scripts that can be run to show various plots or launch pipelines.
All are written in Python and located in `./Script`. We assume that the encoder
was launched and that the `.csv` files given as arguments exist. For the sake
of clarity, we give the ones resulting from the example invocation in the previous
section, but they could be easily adapted to other videos and settings.

### Plot bitsizes and PSNR / frame

```bash
python3 plot_normal.py table_16_16_8_0.csv
python3 plot_log.py table_16_16_8_0.csv
```

One can give the `-nopsnr` argument at the end to show only bitsizes.

### Plot only PSNR / frame

```bash
python3 plot_psnr_per_frame.py table_16_16_8_0.csv
```

### Plot PSNR for given encoder with several QPs

```bash
python3 plot_psnr.py ../ICSPCodec/data/table_cif\(352X288\)_300f.yuv 2 7 20 4 cabac
```

where we have the following arguments:

- YUV video (here `../ICSPCodec/data/table_cif\(352X288\)_300f.yuv`)
- Four QP values between 2 and 48 (here `2 7 20 4`)
    - This one is optional and, if not given, the script will run for all QPs between 2 and 48)
- Encoder (here `cabac`)

### Plot PSNR for several encoders with several QPs

```bash
python3 plot_psnr_ec.py ../ICSPCodec/data/table_cif\(352X288\)_300f.yuv 2 4 7 20 cabac original
```

where we have the following arguments:

- YUV video (here `../ICSPCodec/data/table_cif\(352X288\)_300f.yuv`)
- Either:
    - Four QP values between 2 and 48 in increasing order (here `2 4 7 20`)
    - Simply `all` to run for all QPs between 2 and 48
- First encoder (here `cabac`)
- Second encoder (here `original`)

### Plot histograms (by value & by bitsize)

By bitsize:

```bash
python encoding_histograms.py ../ICSPCodec/results/hist_bitsize_table_16_16_8.csv
```

By value:

```bash
python encoding_histograms_values.py ../ICSPCodec/results/hist_value_table_16_16_8.csv
```

### Launch scalable pipeline and show PSNR / frame

```bash
python3 scalable_pipeline.py table_cif\(352X288\)_300f.yuv 10 8 huffman
```

where we have the following arguments:

- YUV video without prefix (here `table_cif\(352X288\)_300f.yuv`)
- One QP value between 2 and 48 (here `10`)
- Intra frame period (here `8`)
- Encoder (here `huffman`)

### Launch pipeline to show motion vectors

```bash
python3 motion_vectors.py table
```

where we have the following argument:

- Name of a YUV video, without prefix nor suffix (here `table`)

### Launch pipeline to show motion vectors with FFMPEG

```bash
python3 motion_vectors_ffmpeg.py table
```

where we have the following argument:

- Name of a YUV video, without prefix nor suffix (here `table`)

## Credits

- The original implementation at [`JawThrow/ICSPCodec`](https://github.com/JawThrow/ICSPCodec)
