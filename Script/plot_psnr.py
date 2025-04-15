import sys
import os
import pandas as pd
import matplotlib.pyplot as plt
import subprocess


"""
Script to plot and generate psnr vs bitrate
"""
def plotDataFrame(dataFrame,intraPeriod: list):
    fig, ax1 = plt.subplots(figsize=(10, 6))
    maxPsnr = dataFrame.filter(like="psnr_").max().max()
    maxBitrate = dataFrame.filter(like="bitrate_").max().max()
    minPsnr = dataFrame.filter(like="psnr_").min().min()
    minBitrate = dataFrame.filter(like="bitrate_").min().min()
    mark = ["^","o","s","x"]
    for i in range(len(intraPeriod)):
        if intraPeriod[i] == 0:
            l = "All intra"
        elif intraPeriod[i] == 300:
            l = "1 intra"
        else:
            l = f"Intraperiod {intraPeriod[i]}"
        ax1.plot(dataFrame[f"bitrate_{intraPeriod[i]}"], dataFrame[f"psnr_{intraPeriod[i]}"],marker=mark[i%4], label=l)
    ax1.set_xlabel("Bit rate (Mbps)")
    ax1.set_ylabel("PSNR (db)", color="blue")
    ax1.set_ylim(minPsnr-1, maxPsnr+1)
    ax1.set_xlim(minBitrate-1,maxBitrate+1)
    ax1.tick_params(axis="y", labelcolor="blue")
    ax1.set_title("Bit rate vs PSNR")
    ax1.grid(True)
    ax1.legend()
    plt.tight_layout()
    plt.show()

"""
need 5 argument YUV file path and 4 Qp's
"""
argSize = len(sys.argv)
if (argSize > 3 and argSize < 6) or argSize < 3 :
    print("too few arg, either file + 4 qp's or  file + all for all qp's between 2 and 48")
    sys.exit(1)



yuvFile = sys.argv[1]
if not os.path.exists(yuvFile):
    print(f"Error: file {yuvFile} does not exist.")
    sys.exit(1)

name = os.path.basename(yuvFile)
name = name.split("_")[0]
print("Before change:", os.getcwd())
# change dir so path doesn't break in codec 
# TODO change it so it doesn't have to account for that
os.chdir("../ICSPCodec/build/Release/")
print("After change:", os.getcwd())
#init all the dict to keep the values
qps = [int(sys.argv[i]) for i in range(2,6)] if argSize > 3 else [i for i in range(2,49)]
intraPeriod = [0,8,16,32,300]
bitrate = {f'bitrate_{key}' : [] for key in intraPeriod}
psnrs = {f'psnr_{key}': [] for key in intraPeriod}
print(f"File: {yuvFile}\n--> Qp's: {qps} for inter values: {intraPeriod}")
yuvFile = f'../../data/{os.path.basename(yuvFile)}'
print(f"File after change: {yuvFile}\n--> Qp's: {qps} for inter values: {intraPeriod}")
# generating all values by running the codec on the yuv file for all intraperiods and qps
for i in range(len(qps)):
    for j in range(len(intraPeriod)):
        if(not os.path.exists(f"../../results/{name}_{qps[i]}_{qps[i]}_{intraPeriod[j]}.csv")):
            print(f"Generating data for qp: {qps[i]} - inter: {intraPeriod[j]}")
            commandArgs = [f"{os.getcwd()}/ICSPCodec", "-i", yuvFile, "-n", "300", "-q", f"{qps[i]}", "--intraPeriod",f"{intraPeriod[j]}","--EnMultiThread","0"]
            result = subprocess.run(commandArgs, capture_output=True, text=True,cwd=os.getcwd())
        else:
            print(f"Data for qp: {qps[i]} - inter: {intraPeriod[j]} exists skipping..")

# extracting all data into dict
for qp in qps:
    for intra in intraPeriod:
            csvFilename = f"../../results/{name}_{qp}_{qp}_{intra}.csv"
            print(f"reading: {csvFilename}")
            if not os.path.exists(csvFilename):
                print(f"Error: file {csvFilename} does not exist.")
                sys.exit(1)
            # Read the CSV using semicolon as separator and point as decimal (US format)
            dataFrame = pd.read_csv(csvFilename, sep=";", decimal=".")
            psnr = dataFrame['AvgPSNR'].iloc[-1]
            print(f"avg psnr {psnr}")
            binPath = f"../../results/{name}_compCIF_{qp}_{qp}_{intra}.bin"
            try:
                size = os.path.getsize(binPath) * 8
            except Exception as e:
                print(f"Error reading file {binPath}: {e}")
                size = 0
            mbps = round((size / 10) / (1000**2), 2)
            bitrate[f'bitrate_{intra}'].append(mbps)
            psnrs[f'psnr_{intra}'].append(psnr)

# panda frame usage to organise data
df1 = pd.DataFrame(psnrs)
df2 = pd.DataFrame(bitrate)
dft = pd.concat([df1, df2], axis=1)
sorted_dft = dft.sort_values(by='bitrate_0', ascending=True)
zero = pd.DataFrame({key : [0] for key in sorted_dft.columns})
df = pd.concat([zero, sorted_dft], ignore_index=True)
plotDataFrame(sorted_dft, intraPeriod)
