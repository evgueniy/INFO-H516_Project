# INFO-H516_Project
## Recquirements
python3 libraries: pandas, bjontegaard
will do recquirement.txt after
## QP Values
2 6 20 42
## Output
The following command:
```bash

./ICSPCodec  -i  "../../data/table_cif(352X288)_300f.yuv"  -n  300  -q  16  --intraPeriod  8  --EnMultiThread  0

```
will produce **two output files** in the ICSPCodec/results file directory for bin and csv but not for yuv:

-  **Encoded Binary File:**  `table_comCIF_16_16_8.bin`

-  **Decoded YUV File:**  `table_cif(352X288)_300f.yuv_16_16_8_decoded.yuv`
-  **CSV File:**  `table_16_16_8.csv`

