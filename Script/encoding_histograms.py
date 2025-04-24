import sys
import os
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

def plot_encoding_histogram(hist_path):
    df = pd.read_csv(hist_path, sep=";", decimal=".")
    # print(dfs.head())

    # Create plt
    fig, axs = plt.subplots(2, 2, figsize=(10, 6))
    print(axs[0])

    # Plot data
    for ax, (label, df) in zip(axs.flatten(), df.groupby('Type')):
        ax.set_xlabel("Size (bits)")
        ax.set_ylabel("Number of values encoded")
        ax.set_title(f"Histogram for {label}")
        ax.bar(df["BitSize"], df["Count"])

    plt.tight_layout()
    plt.show()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Error: no histogram .csv file given as argument.")
        sys.exit(1)

    hist_csv_path = sys.argv[1]
    if not os.path.exists(hist_csv_path):
        print(f"Error: file {hist_csv_path} does not exist.")
        sys.exit(1)

    plot_encoding_histogram(hist_csv_path)

