import os
import sys

# import tempfile
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

from pathlib import Path


def generate_mv_video(mv_path, dst_dir):
    # Load motion vectors
    df = pd.read_csv(mv_path, sep=";")
    gdf = df.groupby("Frame", sort=True)

    # Save individual images
    dst_dir = Path(dst_dir)
    for frm, mv in gdf:
        fig, ax = plt.subplots(figsize=(11, 8), frameon=False)  # CIF’s ratio
        ax.set_xlim([0, 352])
        ax.set_ylim([0, 288])
        ax.invert_yaxis()
        ax.axis("off")
        # nx = mv["MvDirX"]
        # ny = mv["MvDirY"]
        nx = mv["MvDirX"] / (np.sqrt(mv["MvDirX"]) ** 2 + np.sqrt(mv["MvDirY"] ** 2))
        ny = mv["MvDirY"] / (np.sqrt(mv["MvDirX"]) ** 2 + np.sqrt(mv["MvDirY"] ** 2))
        ax.quiver(mv["MvPosX"], mv["MvPosY"], nx, ny)
        out_name = dst_dir.joinpath(f"{frm:03d}-mv.png")
        plt.savefig(out_name, transparent=True, bbox_inches="tight", pad_inches=0)
        # plt.show()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("[Error] Please provide:\n\t- a file with the motion vectors\n\t- a directory where the images should be saved.")
        exit(1)

    mv_path = sys.argv[1]
    dst_dir = sys.argv[2]
    if not os.path.exists(mv_path) or not os.path.exists(dst_dir):
        print("[Error] One of the provided paths doesn’t exist.")
        exit(1)

    generate_mv_video(mv_path, dst_dir)
