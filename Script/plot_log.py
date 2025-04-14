import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("table_cif(352X288)_300f.yuv_16_16_8.csv", sep=';')

df["TotalDC"] += 1
df["TotalAC"] += 1
df["TotalMV"] += 1
df["TotalEntropy"] += 1

plt.figure(figsize=(12, 6))
plt.plot(df["Frame"], df["TotalDC"], label="DC Bits", linestyle='--')
plt.plot(df["Frame"], df["TotalAC"], label="AC Bits")
plt.plot(df["Frame"], df["TotalMV"], label="Motion Bits", linestyle='--')
plt.plot(df["Frame"], df["TotalEntropy"], label="Total Entropy", linewidth=2)

plt.yscale("log")
plt.xlabel("Frame")
plt.ylabel("Bits (log scale)")
plt.title("Distribution des bits par frame (échelle logarithmique)")
plt.legend()
plt.grid(True, which='both', linestyle='--', linewidth=0.5)
plt.tight_layout()
plt.savefig("bitrate_plot_logscale.png")
plt.show()
