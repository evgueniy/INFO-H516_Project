import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

def plot_psrn_per_frame(stat_path):
    df = pd.read_csv(stat_path, sep=";", decimal=".")
    print(df.head())

    # Create plt
    fig, ax1 = plt.subplots(figsize=(10, 6))

    # Plot data
    ax1.plot(df["Frame"], df["PSNR"], marker='.', label="PSNR")
    ax1.plot(df["Frame"], df["AvgPSNR"], label="Average PSNR")
    ax1.set_xlabel("Frame (number)")
    ax1.set_ylabel("PSNR (dB)")
    ax1.set_title("PSNR per frame")
    ax1.legend(loc="best")
    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Error: no statistics .csv file given as argument.")
        sys.exit(1)

    stat_csv_path = sys.argv[1]
    if not os.path.exists(stat_csv_path):
        print(f"Error: file {stat_csv_path} does not exist.")
        sys.exit(1)

    plot_psrn_per_frame(stat_csv_path)
