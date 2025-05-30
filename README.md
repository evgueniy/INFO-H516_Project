
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
```bash
./ICSPCodec [option] [values]
-i : input yuv sequence
-n : the number of frames(default is 1)
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

Optionnal -e parameter sets the entropy coder that will be used. 
1) It must either absent and use its original entropy coder 
2) Or must be one of the following parameters:
	-  **orginal** 0 (can be achieved by omitting it)
	-  **abac** 1
	-  **huffman** 2
	-  **cabac** 3

It determines the last suffix on csv statistics and bin compressed bitstream files.

Running it produces the following files found under `./results` (the example

file names are resulting from the above invocation):

  

-  **Encoded Binary File:**  `table_comCIF_16_16_8_0.bin`

-  **Decoded YUV File:**  `table_cif(352X288)_300f.yuv_16_16_8_decoded.yuv` (in data folder)

-  **Statistics File:**  `table_16_16_8_0.csv`
## Scripts
Optional parameters are indicated by * .
### plot_psnr.py
**Parameters**:
1)  YUV file
2) 4 Qp * -> 2 7 20 42 
3)  encoder

example:
```bash
python3 plot_psnr.py ../ICSPCodec/data/table_cif\(352X288\)_300f.yuv 2 7 20 4 cabac
```
Generate a graph Psnr over bitrate with 4 point at the 4 Qps and displays the BD-rate in reference to the all intra encoding. If no Qp are given, it will genereate points between 2 and 48.
If datapoint doesn't exist, it will run the encoder on the file to generate the data.

### plot_psnr_ec.py
**Parameters**:
1)  YUV file
2) 4 Qp  -> 2 7 20 42 | all ( between2 and 48) 
3) encoder1
4) encoder2

example:
```bash
python3 plot_psnr_ec.py ../ICSPCodec/data/table_cif\(352X288\)_300f.yuv 2 7 20 4 cabac original
```
Plot several graphs Psnr over bitrate with 4 point at the 4 Qps and displays the BD-rate in reference to the first entropy coder. If no Qp are given, it will genereate points between 2 and 48.
If datapoint doesn't exist, it will run the encoder on the file to generate the data for the encoder or both encoders.

  ### plot_psnr_per_frame.py  
**Parameters**:
1)  CSV file

example:
```bash
python3 plot_psnr_per_frame.py ../ICSPCodec/results/table_16_16_8_0.csv
```
Plot a a graph with psnr per per frame.


  ### plot_normal.py | plot_log.py 
**Parameters**:
1)  CSV file
2) -nopsnr *

example:
```bash
python3 plot_normal.py ../ICSPCodec/results/table_16_16_8_0.csv
```
Plota graph wtih AC, DC, Mv entropy per frame with Psnr per frame if not deactivated.
<!-- ## QP Values -->

<!-- 2 7 20 42 -->

  

## Credits

  

- The original implementation at [`JawThrow/ICSPCodec`](https://github.com/JawThrow/ICSPCodec)
