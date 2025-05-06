import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import subprocess
from bjontegaard import bd_rate, bd_psnr

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

argSize = len(sys.argv)
if argSize not in (3, 7):
    print("too few arg, either file + encoding or file + 4 qp's + encoding (all qp's between 2 and 48)")
    sys.exit(1)

yuvFile = sys.argv[1]
if not os.path.exists(yuvFile):
    print(f"Error: file {yuvFile} does not exist.")
    sys.exit(1)

if argSize == 7:
    qps = [int(sys.argv[i]) for i in range(2,6)]
    encodingArg = sys.argv[6]
elif argSize == 3:
    qps = [i for i in range(2,49)]
    encodingArg = sys.argv[2]

if encodingArg.lower() == "original":
    enc_suffix = "0"
elif encodingArg.lower() == "cabac":
    enc_suffix = "1"
elif encodingArg.lower() == "huffman":
    enc_suffix = "2"
else:
    print("Invalid encoding option. Must be one of original, cabac, huffman")
    sys.exit(1)

name = os.path.basename(yuvFile)
name = name.split("_")[0]
print("Before change:", os.getcwd())
os.chdir("../ICSPCodec/build/Release/")
print("After change:", os.getcwd())

print(f"File: {yuvFile}\n--> Qp's: {qps} for inter values: [0,8,16,32,300]")
yuvFile = f'../../data/{os.path.basename(yuvFile)}'
print(f"File after change: {yuvFile}\n--> Qp's: {qps} for inter values: [0,8,16,32,300]")

intraPeriod = [0,8,16,32,300]
bitrate = {f'bitrate_{key}' : [] for key in intraPeriod}
psnrs = {f'psnr_{key}': [] for key in intraPeriod}

for i in range(len(qps)):
    for j in range(len(intraPeriod)):
        csvFilename = f"../../results/{name}_{qps[i]}_{qps[i]}_{intraPeriod[j]}_{enc_suffix}.csv"
        if not os.path.exists(csvFilename):
            print(f"Generating data for qp: {qps[i]} - inter: {intraPeriod[j]} - encoder: {encodingArg}")
            commandArgs = [f"{os.getcwd()}/ICSPCodec", "-i", yuvFile, "-n", "300", "-q", f"{qps[i]}", "--intraPeriod", f"{intraPeriod[j]}", "--EnMultiThread", "0","-e",encodingArg]
            subprocess.run(commandArgs, capture_output=True, text=True, cwd=os.getcwd())
        else:
            print(f"Data for qp: {qps[i]} - inter: {intraPeriod[j]} exists skipping..")

for qp in qps:
    for intra in intraPeriod:
        csvFilename = f"../../results/{name}_{qp}_{qp}_{intra}_{enc_suffix}.csv"
        print(f"reading: {csvFilename}")
        if not os.path.exists(csvFilename):
            print(f"Error: file {csvFilename} does not exist.")
            sys.exit(1)
        dataFrame = pd.read_csv(csvFilename, sep=";", decimal=".")
        psnr = dataFrame['AvgPSNR'].iloc[-1]
        print(f"avg psnr {psnr}")
        binPath = f"../../results/{name}_compCIF_{qp}_{qp}_{intra}_{enc_suffix}.bin"
        try:
            size = os.path.getsize(binPath) * 8
        except Exception as e:
            print(f"Error reading file {binPath}: {e}")
            size = 0
        mbps = round((size / 10) / (1000**2), 2)
        bitrate[f'bitrate_{intra}'].append(mbps)
        psnrs[f'psnr_{intra}'].append(psnr)

df1 = pd.DataFrame(psnrs)
df2 = pd.DataFrame(bitrate)
dft = pd.concat([df1, df2], axis=1)
sorted_dft = dft.sort_values(by='bitrate_0', ascending=True)
zero = pd.DataFrame({key : [0] for key in sorted_dft.columns})
df = pd.concat([zero, sorted_dft], ignore_index=True)
plotDataFrame(sorted_dft, intraPeriod,encodingArg)
