import os
import sys
import subprocess

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
        nx = mv["MvDirX"]
        ny = mv["MvDirY"]

        ax.quiver(mv["MvPosX"], mv["MvPosY"], nx, ny, angles="xy", width=0.002, scale_units="xy", scale=.8)
        out_name = dst_dir.joinpath(f"{frm:03d}-mv.png")
        plt.savefig(out_name, transparent=True, bbox_inches="tight", pad_inches=0)


def main(video_name):
    video_file = f"{video_name}_cif(352X288)_300f.yuv"
    mv_file = f"mv_{video_name}_.csv"

    print(f"-- Generating motion vectors for {video_name} (file {video_file})")

    print("-- Running the encoder to get motion vectors")
    os.chdir("../ICSPCodec/build/Debug")
    subprocess.run(f"./ICSPCodec -i \"../../data/{video_file}\" -n 300 -q 16 --intraPeriod 8", shell=True, check=True)

    out_dir = f"../ICSPCodec/results/mv_{video_name}"
    print(f"-- Generating images in {out_dir}")
    os.chdir("../../../Script")
    if not os.path.exists(out_dir):
        os.mkdir(out_dir)
    generate_mv_video(f"../ICSPCodec/results/mv_{video_name}_.csv", out_dir)

    print("-- Rescaling original YUV video")
    base_file = f"{out_dir}/base.mp4"
    subprocess.run(f"ffmpeg -framerate 30 -s 352x288 -i \"../ICSPCodec/data/{video_file}\" -vf scale=852x616 \"{base_file}\"", shell=True, check=True)

    print("-- Combining videos")
    out_file = f"{out_dir}/out.mp4"
    subprocess.run(f"ffmpeg -i \"{base_file}\" -framerate 30 -i \"{out_dir}/%3d-mv.png\" -filter_complex \"blend=multiply\" -c:v libx264 -pix_fmt yuv420p \"{out_file}\"", shell=True, check=True)

    subprocess.run(f"xdg-open {out_file}", shell=True, check=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("[Error] Please provide:\n\t- a video file (just the name, like `table`).")
        exit(1)

    file_name = sys.argv[1]
    main(file_name)
