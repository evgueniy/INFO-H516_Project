from bjontegaard import bd_rate, bd_psnr

## Test values !!!!
rate_anchor = [9487.76, 4593.60, 2486.44, 1358.24]
psnr_anchor = [40.037, 38.615, 36.845, 34.851]

rate_test = [9787.80, 4469.00, 2451.52, 1356.24]
psnr_test = [40.121, 38.651, 36.970, 34.987]


bd_r = bd_rate(rate_anchor, psnr_anchor, rate_test, psnr_test, method="pchip")
bd_p = bd_psnr(rate_anchor, psnr_anchor, rate_test, psnr_test, method="pchip")

print(f"BD-Rate: {bd_r:.2f} %")
print(f"BD-PSNR: {bd_p:.2f} dB")