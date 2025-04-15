import pandas as pd
import matplotlib.pyplot as plt
import sys

argSize = len(sys.argv)
if argSize < 2:
    print("Too few arguments.")
    sys.exit(1)

# Optionally disable PSNR plot using the "-nopsnr" flag
psnr_on = True
if "-nopsnr" in sys.argv:
    psnr_on = False

print(sys.argv)
df = pd.read_csv(sys.argv[1], sep=';')

fig, ax1 = plt.subplots(figsize=(12, 6))

ax1.plot(df["Frame"], df["TotalDC"], label="DC Bits")
ax1.plot(df["Frame"], df["TotalAC"], label="AC Bits")
ax1.plot(df["Frame"], df["TotalMV"], label="Motion Bits")
ax1.plot(df["Frame"], df["TotalEntropy"], label="Total Entropy", linewidth=2)
ax1.set_xlabel("Frame")
ax1.set_ylabel("Bits")
ax1.set_title("Distribution des bits par frame")
ax1.grid(True)

if psnr_on:
    ax2 = ax1.twinx()
    ax2.plot(df["Frame"], df["PSNR"], color="blue", label="PSNR", linestyle="--", linewidth=2)
    ax2.set_ylabel("PSNR", color="red")
    ax2.tick_params(axis="y", labelcolor="red")
    
    handles1, labels1 = ax1.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    combined = {}
    for handle, label in zip(handles1 + handles2, labels1 + labels2):
        if label not in combined:
            combined[label] = handle
    ax1.legend(combined.values(), combined.keys(), loc="best")
else:
    ax1.legend(loc="best")

fig.tight_layout()
plt.savefig("bit_distribution_plot.png")
plt.show()
