import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import subprocess
from bjontegaard import bd_rate, bd_psnr
import re
import utils

def plotDataFrame(dataFrame, intraPeriod: list,codec):
    fig, ax1 = plt.subplots(figsize=(10, 6))
    maxPsnr = dataFrame.filter(like="psnr_").max().max()
    maxBitrate = dataFrame.filter(like="bitrate_").max().max()
    minPsnr = dataFrame.filter(like="psnr_").min().min()
    minBitrate = dataFrame.filter(like="bitrate_").min().min()
    mark = ["^", "o", "s", "x", "D"]
    for i in range(len(intraPeriod)):
        if intraPeriod[i] == 0:
            l = "All intra (ref)"
        elif intraPeriod[i] == 300:
            l = "1 intra"
        else:
            l = f"Intraperiod {intraPeriod[i]}"
        ax1.plot(dataFrame[f"bitrate_{intraPeriod[i]}"], dataFrame[f"psnr_{intraPeriod[i]}"],
                 marker=mark[i % 5], label=l)
    ax1.set_xlabel("Bit rate (Mbps)")
    ax1.set_ylabel("PSNR (db)", color="blue")
    ax1.set_ylim(minPsnr - 1, maxPsnr + 1)
    ax1.set_xlim(minBitrate - 1, maxBitrate + 1)
    ax1.tick_params(axis="y", labelcolor="blue")
    ax1.set_title(f"Bit rate vs PSNR - {codec}")
    ax1.grid(True)
    ax1.legend()
    if len(dataFrame["bitrate_0"].values) == 4:
        refRate = dataFrame["bitrate_0"].values
        refPsnr = dataFrame["psnr_0"].values
        bdLines = []
        for intra in intraPeriod:
            if intra == 0:
                continue
            elif intra == 300:
                l = "1 intra"
            else:
                l = f"Intra {intra}"
            testRate = dataFrame[f"bitrate_{intra}"].values
            testPsnr = dataFrame[f"psnr_{intra}"].values
            bdR = bd_rate(refRate, refPsnr, testRate, testPsnr, method="pchip")
            bdP = bd_psnr(refRate, refPsnr, testRate, testPsnr, method="pchip")
            bdLines.append(f"{l}:\nBD-Rate: {bdR:.2f}%\nBD-PSNR: {bdP:.2f} dB")
        bdText = "\n\n".join(bdLines)
        plt.gcf().text(0.80, 0.50, bdText, bbox=dict(facecolor="white", edgecolor="black", alpha=0.5))
    
    plt.tight_layout()
    plt.show()

def main(path: str, qps: list[int,], entropyCoder: str) -> None:
    # == setup == 
    file = utils.YUV(path)
    mode = "Debug"
    ICSP = utils.ICSPCodec(entropyCoder, mode)
    ICSP.getICSPWorkDir()
    intraPeriod = [0,8,16,32,300]
    bitrate = {f'bitrate_{key}' : [] for key in intraPeriod}
    psnrs = {f'psnr_{key}': [] for key in intraPeriod}
    # ==
    print(f"File: {file.getFileName()}\n--> Qp's: {qps} for inter values: [0,8,16,32,300]")
    # == loop and generate missing files necessary for comparison
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

    # == organising data in pandas frames
    df1 = pd.DataFrame(psnrs)
    df2 = pd.DataFrame(bitrate)
    dft = pd.concat([df1, df2], axis=1)
    sorted_dft = dft.sort_values(by='bitrate_0', ascending=True)
    zero = pd.DataFrame({key : [0] for key in sorted_dft.columns})
    df = pd.concat([zero, sorted_dft], ignore_index=True)
    plotDataFrame(sorted_dft, intraPeriod,encodingArg)

if __name__ == "__main__":
    argSize = len(sys.argv)
    if argSize not in (3, 7):
        print("too few arg, either file + encoding or file + 4 qp's + encoding (all qp's between 2 and 48)")
        sys.exit(1)

    yuvFile = sys.argv[1]
    if not os.path.exists(yuvFile):
        print(f"Error: file {yuvFile} does not exist.")
        sys.exit(1)
    # if encoded with file then 4 qp and an encoder
    if argSize == 7:
        qps = [int(sys.argv[i]) for i in range(2,6)]
        encodingArg = sys.argv[6]
    # if encoded with file then encoder , it will plot for all qp's between 2 and 48
    elif argSize == 3:
        qps = [i for i in range(2,49)]
        encodingArg = sys.argv[2]
    if encodingArg.lower() not in ("cabac","huffman","original","abac"):
        sys.exit(1)
    # === IF all arg are valid then we proceed to main
    main(yuvFile, qps, encodingArg)
