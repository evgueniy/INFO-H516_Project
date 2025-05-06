import sys
import os
import math
import pandas as pd
import matplotlib.pyplot as plt
import subprocess
from bjontegaard import bd_rate, bd_psnr

def plot_all_intra_subplots(results, intraPeriod: list):
    num_intra = len(intraPeriod)
    ncols = 2
    nrows = math.ceil(num_intra / ncols)
    fig, axs = plt.subplots(nrows, ncols, figsize=(12, 5 * nrows))
    axs = axs.flatten()
    markers = {"original": "^", "cabac": "o", "huffman": "s"}
    for idx, intra in enumerate(intraPeriod):
        ax = axs[idx]
        all_bit = []
        all_psnr = []
        for enc in results:
            all_bit.extend(results[enc]['bitrate'][f'bitrate_{intra}'])
            all_psnr.extend(results[enc]['psnr'][f'psnr_{intra}'])
        minBitrate = min(all_bit) if all_bit else 0
        maxBitrate = max(all_bit) if all_bit else 10
        minPsnr = min(all_psnr) if all_psnr else 0
        maxPsnr = max(all_psnr) if all_psnr else 50
        for enc in results:
            m = markers.get(enc, "x")
            x = results[enc]['bitrate'][f'bitrate_{intra}']
            y = results[enc]['psnr'][f'psnr_{intra}']
            ax.plot(x, y, marker=m, label=enc.capitalize())
        if len(results) == 2:
            keys = list(results.keys())
            ref_enc = keys[0]
            test_enc = keys[1]
            refRate = results[ref_enc]['bitrate'][f'bitrate_{intra}']
            refPsnr = results[ref_enc]['psnr'][f'psnr_{intra}']
            testRate = results[test_enc]['bitrate'][f'bitrate_{intra}']
            testPsnr = results[test_enc]['psnr'][f'psnr_{intra}']
            bdR = bd_rate(refRate, refPsnr, testRate, testPsnr, method="pchip")
            ax.text(0.95, 0.05, f"BD-Rate: {bdR:.2f}%", transform=ax.transAxes,
                    horizontalalignment='right', verticalalignment='bottom', bbox=dict(facecolor="white", alpha=0.5))
        title = "All intra (ref)" if intra == 0 else ("1 intra" if intra == 300 else f"Intra period {intra}")
        ax.set_title(title)
        ax.set_xlabel("Bit rate (Mbps)")
        ax.set_ylabel("PSNR (dB)")
        ax.set_xlim(minBitrate - 1, maxBitrate + 1)
        ax.set_ylim(minPsnr - 1, maxPsnr + 1)
        ax.grid(True)
        ax.legend()
    for i in range(num_intra, len(axs)):
        axs[i].set_visible(False)
    plt.tight_layout()
    plt.show()

argSize = len(sys.argv)
if argSize < 3:
    print("too few arg, usage: file [all | qp1 qp2 qp3 qp4] encoder [optional second encoder]")
    sys.exit(1)
yuvFile = sys.argv[1]
if not os.path.exists(yuvFile):
    print(f"Error: file {yuvFile} does not exist.")
    sys.exit(1)
if sys.argv[2].lower() == "all":
    if argSize not in (4, 5):
        print("Error: for 'all' mode, usage: file all encoder [optional second encoder]")
        sys.exit(1)
    qps = [i for i in range(2, 49)]
    if argSize == 4:
        encoders = [sys.argv[3]]
    else:
        encoders = [sys.argv[3], sys.argv[4]]
else:
    if argSize not in (7, 8):
        print("Error: for explicit qp mode, usage: file qp1 qp2 qp3 qp4 encoder [optional second encoder]")
        sys.exit(1)
    qps = [int(sys.argv[i]) for i in range(2, 6)]
    if argSize == 7:
        encoders = [sys.argv[6]]
    else:
        encoders = [sys.argv[6], sys.argv[7]]
enc_map = {"original": "0", "cabac": "1", "huffman": "2"}
encs = {}
for enc in encoders:
    enc_lower = enc.lower()
    if enc_lower not in enc_map:
        print("Invalid encoding option. Must be one of original, cabac, huffman")
        sys.exit(1)
    encs[enc_lower] = enc_map[enc_lower]
name = os.path.basename(yuvFile)
name = name.split("_")[0]
print("Before change:", os.getcwd())
os.chdir("../ICSPCodec/build/Release/")
print("After change:", os.getcwd())
print(f"File: {yuvFile}\n--> Qp's: {qps} for inter values: [0, 8, 16, 32, 300]")
yuvFile = f"../../data/{os.path.basename(yuvFile)}"
print(f"File after change: {yuvFile}\n--> Qp's: {qps} for inter values: [0, 8, 16, 32, 300]")
intraPeriod = [0, 8, 16, 32, 300]
results = {}
for enc in encs:
    bitrate = {f"bitrate_{ip}": [] for ip in intraPeriod}
    psnrs = {f"psnr_{ip}": [] for ip in intraPeriod}
    for i in range(len(qps)):
        for j in range(len(intraPeriod)):
            csvFilename = f"../../results/{name}_{qps[i]}_{qps[i]}_{intraPeriod[j]}_{encs[enc]}.csv"
            if not os.path.exists(csvFilename):
                print(f"Generating data for qp: {qps[i]} - inter: {intraPeriod[j]} - encoder: {enc}")
                commandArgs = [f"{os.getcwd()}/ICSPCodec", "-i", yuvFile, "-n", "300", "-q", f"{qps[i]}",
                               "--intraPeriod", f"{intraPeriod[j]}", "--EnMultiThread", "0", "-e", enc]
                subprocess.run(commandArgs, capture_output=True, text=True, cwd=os.getcwd())
            else:
                print(f"Data for qp: {qps[i]} - inter: {intraPeriod[j]} exists skipping..")
    for qp in qps:
        for intra in intraPeriod:
            csvFilename = f"../../results/{name}_{qp}_{qp}_{intra}_{encs[enc]}.csv"
            print(f"reading: {csvFilename}")
            if not os.path.exists(csvFilename):
                print(f"Error: file {csvFilename} does not exist.")
                sys.exit(1)
            dataFrame = pd.read_csv(csvFilename, sep=";", decimal=".")
            psnr = dataFrame["AvgPSNR"].iloc[-1]
            print(f"avg psnr {psnr}")
            binPath = f"../../results/{name}_compCIF_{qp}_{qp}_{intra}_{encs[enc]}.bin"
            try:
                size = os.path.getsize(binPath) * 8
            except Exception as e:
                print(f"Error reading file {binPath}: {e}")
                size = 0
            mbps = round((size / 10) / (1000**2), 2)
            bitrate[f"bitrate_{intra}"].append(mbps)
            psnrs[f"psnr_{intra}"].append(psnr)
    results[enc] = {"bitrate": bitrate, "psnr": psnrs}
plot_all_intra_subplots(results, intraPeriod)
