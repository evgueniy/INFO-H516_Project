#!/usr/bin/env bash

OUT_DIR="./results/mv_$1_ffplay"
mkdir -p "$OUT_DIR"
cd "$OUT_DIR" || exit

if [ ! -f "converted.mp4" ]; then
    ffmpeg -framerate 30 -s 352x288 -i ../../data/"$1"_cif\(352X288\)_300f.yuv converted.mp4
fi

ffplay -i "converted.mp4" -flags2 +export_mvs -vf codecview=mv=pf+bf+bb

