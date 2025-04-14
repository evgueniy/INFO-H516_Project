import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

if len(sys.argv) < 2:
    print("too few arg")
    sys.exit(1)

csvFilename = sys.argv[1]

if not os.path.exists(csvFilename):
    print(f"Error: file {csvFilename} does not exist.")
    sys.exit(1)

# Read the CSV using semicolon as separator and comma as decimal (because of BE or european format)
dataFrame = pd.read_csv(csvFilename, sep=";", decimal=",")
print(dataFrame.head())

fig, ax1 = plt.subplots(figsize=(10, 6))

ax1.plot(dataFrame["Frame"], dataFrame["TotalDC"], marker='o', label="DC Coding")
ax1.plot(dataFrame["Frame"], dataFrame["TotalAC"], marker='s', label="AC Coding")
ax1.plot(dataFrame["Frame"], dataFrame["TotalMV"], marker='^', label="Motion Coding")
ax1.plot(dataFrame["Frame"], dataFrame["TotalEntropy"], marker='x', label="Total Entropy")
ax1.set_xlabel("Frame")
ax1.set_ylabel("Bits", color="blue")
ax1.tick_params(axis="y", labelcolor="blue")
#if avg psnr is missing 
# need to just rename it psnr as we need to only use Luma (Y) for PSNR
if "avg PSNR" in dataFrame.columns:
    ax2 = ax1.twinx()
    ax2.plot(dataFrame["Frame"], dataFrame["avg PSNR"], marker='v', linestyle='--', color="red", label="avg PSNR")
    ax2.set_ylabel("avg PSNR", color="red")
    ax2.tick_params(axis="y", labelcolor="red")
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc="best")
else:
    ax1.legend(loc="best")

ax1.set_title("Bits and avg PSNR vs. Frame")
ax1.grid(True)
plt.tight_layout()
plt.show()
