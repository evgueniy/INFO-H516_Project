import sys
import os
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

def plot_psrn_per_frame(stat_paths):
    dfs = [pd.read_csv(path, sep=";", decimal=".") for path in stat_paths]
    # print(dfs.head())

    # Create plt
    fig, ax1 = plt.subplots(figsize=(10, 6))
    ax1.set_xlabel("Frame (number)")
    ax1.set_ylabel("PSNR (dB)")
    ax1.set_title("PSNR per frame")

    # Plot data
    for (i, df) in enumerate(dfs):
        c = 'C' + str(i)
        f = Path(stat_paths[i]).name
        ax1.plot(df["Frame"], df["PSNR"], label=f"PSNR ({f})", color=c, ls='-')
        ax1.plot(df["Frame"], df["AvgPSNR"], label=f"Average PSNR ({f})", color=c, ls=':')

    ax1.legend(loc="best")
    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Error: no statistics .csv file given as argument.")
        sys.exit(1)

    stat_csv_paths = sys.argv[1:]
    for path in stat_csv_paths:
        if not os.path.exists(path):
            print(f"Error: file {path} does not exist.")
            sys.exit(1)

    plot_psrn_per_frame(stat_csv_paths)
