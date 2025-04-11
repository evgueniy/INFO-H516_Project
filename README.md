# INFO-H516_Project
## Output
The following command:
```bash

./ICSPCodec  -i  "../../data/table_cif(352X288)_300f.yuv"  -n  300  -q  16  --intraPeriod  8  --EnMultiThread  0

```
will produce **two output files** in the original file directory:

-  **Encoded Binary File:**  `table_comCIF_16_16_8.bin`

-  **Decoded YUV File:**  `table_cif(352X288)_300f.yuv_16_16_8_decoded.yuv`
-  **TO DO CSV File:**  `table_cif(352X288)_300f.yuv_16_16_8.csv`

