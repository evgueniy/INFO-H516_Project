import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("table_cif(352X288)_300f.yuv_16_16_8.csv", sep=';')

plt.figure(figsize=(12, 6))
plt.plot(df["Frame"], df["TotalDC"], label="DC Bits")
plt.plot(df["Frame"], df["TotalAC"], label="AC Bits")
plt.plot(df["Frame"], df["TotalMV"], label="Motion Bits")
plt.plot(df["Frame"], df["TotalEntropy"], label="Total Entropy", linewidth=2)

plt.xlabel("Frame")
plt.ylabel("Bits")
plt.title("Distribution des bits par frame")
plt.legend()
plt.grid(True)
plt.tight_layout()

plt.savefig("bit_distribution_plot.png")
plt.show()
