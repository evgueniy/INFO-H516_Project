import sys
import os
import math
import pandas as pd
import matplotlib.pyplot as plt
import subprocess
from bjontegaard import bd_rate, bd_psnr
import re
import utils

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

def main(path: str, qps: list[int], entropyCoders: list[str]):
    # == setup 
    mode = "Debug"
    file = utils.YUV(path)
    encoders = [utils.ICSPCodec(ec,mode) for ec in entropyCoders]
    encoders[0].getICSPWorkDir()
    intraPeriod = [0,8,16,32,300]
    results = dict()
    # ==
    print(f"File: {file.getFileName()}\n--> Qp's: {qps} for inter values: [0,8,16,32,300]")
    # == loop and generate missing files necessary for comparison
    for i in range(len(encoders)):
        bitrate = {f"bitrate_{ip}": [] for ip in intraPeriod}
        psnrs = {f"psnr_{ip}": [] for ip in intraPeriod}
        ICSP = encoders[i]
        for qp in qps:
            for period in intraPeriod:
                csvFilename = f"../../results/{file.getSequenceName()}_{qp}_{qp}_{period}_{ICSP.getEntropyCoderID()}.csv"
                bin = f"../../results/{file.getSequenceName()}_comp{file.getFormat().upper()}" \
                    + f"_{qp}_{qp}_{period}_{ICSP.getEntropyCoderID()}.bin"
                # = if one of the file csv or bin is missing we try to generate a new one 
                if not os.path.exists(csvFilename) or not os.path.exists(bin):
                    ICSP.run(file, qp, period)
                else:
                    print(f"Data for qp: {qp} - intraPeriod: {period} exists skipping generation..")
                # == data retrieval
                dataFrame = pd.read_csv(csvFilename, sep=";", decimal=".")
                # = avg psnr from last col
                psnr = dataFrame['AvgPSNR'].iloc[-1]
                print(f"avg psnr {psnr}")
                # = size in bits of compressed video file
                size = os.path.getsize(bin) * 8
                # = throughput in megabits per second
                mbps = round((size / file.getDuration()) / (1000**2), 2)
                bitrate[f'bitrate_{period}'].append(mbps)
                psnrs[f'psnr_{period}'].append(psnr)
        results[ICSP.getEntropyCoder()] = {"bitrate": bitrate, "psnr": psnrs}
    plot_all_intra_subplots(results, intraPeriod)



if __name__ == "__main__":
    # == arg verification
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
            encoders = [sys.argv[6].lower()]
        else:
            encoders = [sys.argv[6].lower(), sys.argv[7].lower()]
        for ec in encoders:
            if ec not in ("cabac","original","abac","huffman"):
                print("Error: encoder(s) must be either cabac,original,abac,huffman")
                sys.exit(1)
    main(yuvFile, qps, encoders)