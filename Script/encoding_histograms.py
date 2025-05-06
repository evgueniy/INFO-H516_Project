import sys
import os
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

def find_biggest(df):
    biggest = -1
    for _, row in df.iterrows():
        if row.Count > 0:
            biggest = max(row.BitSize, biggest)
    return biggest

def plot_encoding_histogram(hist_path):
    df = pd.read_csv(hist_path, sep=";", decimal=".")
    # print(dfs.head())

    # Create plt
    fig, axs = plt.subplots(2, 2, figsize=(10, 6))
    fig.suptitle(f'Bitsize histograms for {Path(hist_path).stem}', fontsize=16)

    # Plot data
    for ax, (label, df) in zip(axs.flatten(), df.groupby('Type')):
        biggest = find_biggest(df)
        fdf = df[df['BitSize'] <= biggest]
        fdf = fdf[fdf['BitSize'] >= 2]
        ax.set_xlabel("Size (bits)")
        ax.set_ylabel("Number of values encoded")
        ax.set_title(f"{label}")
        ax.bar(fdf["BitSize"], fdf["Count"])

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

