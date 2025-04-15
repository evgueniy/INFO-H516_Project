# Improving MPEG-1 using modern techniques

This repository holds the end-of-year project for the course *Visual Media
Compression* (INFO-H516) given by Prof. Gauthier Lafruit during the academic
year of 2025 at the *Université Libre de Bruxelles* (ULB).

The goal of the project is to put students in the shoes of MPEG consortium
members. The primary task is to achieve compression improvements over the
basic version of MPEG-1, but it also includes analysis of the results and
regular meetings held in rougly the same vein as the real ones.

## Recquirements

For the codec no additional requirements are needed besides a C++ compiler
and `glibc`.

For the plotting scripts, `python3` is used along with the following libraries (a
`requirements.txt` is also available):

- [pandas](https://pandas.pydata.org/)
- [bjontegaard](https://github.com/FAU-LMS/bjontegaard)

## Compiling & Running

To setup the codec:

```bash
cd ./ICSPCodec/build/Debug
./build.sh
```

To compile it:

```bash
cd ./ICSPCodec/build/Debug
make
```

To run it, see `-h` for information on the parameters. One potential invocation
could be:

```bash
cd ./ICSPCodec/build/Debug
./ICSPCodec  -i  "../../data/table_cif(352X288)_300f.yuv"  -n  300  -q  16  --intraPeriod  8  --EnMultiThread  0
```

Running it produces the following files found under `./results` (the example
file names are resulting from the above invocation):

-  **Encoded Binary File:**  `table_comCIF_16_16_8.bin`
-  **Decoded YUV File:**  `table_cif(352X288)_300f.yuv_16_16_8_decoded.yuv` (not in `./results` currently)
-  **Statistics File:**  `table_16_16_8.csv`

<!-- ## QP Values -->
<!-- 2 6 20 42 -->

## Credits

- The original implementation at [`JawThrow/ICSPCodec`](https://github.com/JawThrow/ICSPCodec)
