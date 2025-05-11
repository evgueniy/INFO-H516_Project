#!/usr/bin/env bash

# Video file
VID_NAME=$1
VID_FILE="${VID_NAME}_cif(352X288)_300f.yuv"
MV_FILE="mv_${VID_NAME}_.csv"

echo "-- Generating motion vectors for ${VID_NAME} (file '${VID_FILE}')"


# Generate the motion vectors
cd ./build/Debug || exit
echo "-- Running the encoder to get motion vectors"
if [ ! -f "../../results/${MV_FILE}" ]; then
    ./ICSPCodec -i "../../data/${VID_FILE}" -n 300 -q 16 --intraPeriod 8
fi

# Generate the mv frames
cd ../../../Script || exit
OUT_DIR="../ICSPCodec/results/mv_${VID_NAME}"
echo "-- Generating images in ${OUT_DIR}"
if [ ! -d "${OUT_DIR}" ]; then
    mkdir "${OUT_DIR}"
    python motion_vectors.py "../ICSPCodec/results/mv_${VID_NAME}_.csv" "${OUT_DIR}"
fi

# Re-scale base video to that of the images
echo "-- Rescaling original YUV video"
BASE_FILE="${OUT_DIR}/base.mp4"
if [ ! -f "${BASE_FILE}" ]; then
    ffmpeg -framerate 30 -s 352x288 -i "../ICSPCodec/data/${VID_FILE}" -vf scale=852x616 "${BASE_FILE}"
fi

# Combine base with vectors
echo "-- Combining videos"
OUT_FILE="${OUT_DIR}/out.mp4"
if [ ! -f "${OUT_FILE}" ]; then
    ffmpeg -i "$BASE_FILE" -framerate 30 -i "${OUT_DIR}/%3d-mv.png" -filter_complex "blend=multiply" -c:v libx264 -pix_fmt yuv420p "$OUT_FILE"
fi

xdg-open "$OUT_FILE"
