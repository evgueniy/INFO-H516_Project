
import os
import subprocess
import shutil
import cv2
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from numpy.typing import NDArray
import re
import sys

# === parameters ===
res = {"qcif": (str(352//2),str(288//2)), "cif": (str(352),str(288)), "4cif": (str(352*2),str(288*2)) }
# regex to capture name, res and nb of frames
pattern = r"(.*)_([4|Q|q]?cif)[(]*[a-zA-Z0-9]*[)]*_([0-9]+)f" 
output_dir = "scalable_output"
yuv_name = sys.argv[1]
# exec_type = 'Debug'
exec_type = 'Release'
# changing to avoid building proper path in c code :) 
print("Before change:", os.getcwd())
os.chdir(f"../ICSPCodec/build/{exec_type}/")
print("After change:", os.getcwd())
mpeg1_executable = f"./ICSPCodec"
qp_value = sys.argv[2]
intra_period = sys.argv[3]
encoder = "original" if len(sys.argv) < 5 else sys.argv[4]
print(f"Used encoder= {encoder}")
# === files ===
data_dir = "../../data"
input_yuv_path = os.path.join(data_dir,yuv_name)

# === folder ===
cpath = os.path.join(data_dir, output_dir)
frames_cif = os.path.join(cpath, "frames_cif")
frames_downscaled = os.path.join(cpath, "frames_qcif")
frames_decoded = os.path.join(cpath, "frames_qcif_decoded")
frames_upscaled = os.path.join(cpath, "frames_qcif_upscaled")
reconstructed_dir = os.path.join(cpath, "reconstructed")

# === file info ===
res = {"qcif": (352//2,288//2), "cif": (352,288), "4cif": (352*2,288*2) }
match = re.search(pattern, yuv_name)
name = match.group(1)
og_format = match.group(2)
down_format = "qcif" if og_format == "cif" else "cif"
width = res[og_format][0]
height = res[og_format][1]
nb_frames = int(match.group(3))
og_res = f"{width}x{height}"
down_res = f"{width//2}x{height//2}"
fps = 30 if match.group(2) == "cif" else 60

def ensure_clean_dir(path):
    if os.path.exists(path):
        shutil.rmtree(path)
    os.makedirs(path)

# cleanup
for folder in [cpath, frames_cif, frames_downscaled, frames_decoded, frames_upscaled, reconstructed_dir]:
    ensure_clean_dir(folder)

def rgb_to_y(img):
    return 0.299 * img[:, :, 2] + 0.587 * img[:, :, 1] + 0.114 * img[:, :, 0]

def getYValues(H: int,W: int, nframes: int, path: str) -> np.ndarray:
    totalY = (W*H) 
    totalUV = (H/2 * W/2) * 2
    luma = np.zeros(shape=(H,W,nframes),dtype=np.float32)
    with open(path, "rb") as f:
        for frame in range(nframes):
            frameOffset = int(totalY + totalUV) * frame
            f.seek(frameOffset)
            buffer = f.read(totalY)
            Y = np.frombuffer(buffer,dtype=np.uint8)
            Y = Y.reshape((H, W)).astype(np.float32)
            luma[:, :, frame] = Y
    return luma

def getPsnr(origin: np.ndarray, comp: np.ndarray ) -> np.ndarray:
    #float 64 for rounding
    psnr = np.zeros(origin.shape[-1], dtype=np.float64)
    for frame in range(origin.shape[-1]):
        mse = np.mean((origin[:,:,frame] - comp[:,:,frame])**2)
        psnr[frame] =  round(10 *np.log10((255**2 / mse)),2) if mse else float('inf')
    return psnr

def displayGraph(psnr: list[NDArray[np.float64]]):
    labels = ["Original","Upscaled", "Reconstructed"]
    nframes = psnr[0].shape[-1]
    plt.figure(figsize=(10, 6))
    x = np.arange(nframes)
    for i, series in enumerate(psnr):
        label = labels[i] if i < len(labels) else f'Sequence {i}'
        plt.plot(x, series, label=label)
    plt.xlabel('Frame')
    plt.ylabel('PSNR (dB)')
    plt.title('PSNR per Frame')
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()
    
# === pipeline ===

print(f"Downscaling {og_format.upper()}...")
downscaled_yuv_name = f"downscaled-{name}_{down_format}_{nb_frames}f.yuv"
downscaled_yuv_input = os.path.join(cpath, downscaled_yuv_name)
decode = f"_{qp_value}_{qp_value}_{intra_period}_decoded.yuv"
subprocess.run(f'ffmpeg -f rawvideo -pix_fmt yuv420p -s {og_res} -i "{input_yuv_path}" -vf "scale={width//2}:{height//2}" -pix_fmt yuv420p -f rawvideo "{downscaled_yuv_input}"', shell=True, check=True)

print(f"Encoding {down_format.upper()}...")
downscaled_yuv_decoded = os.path.join(cpath, downscaled_yuv_name+decode)
print(downscaled_yuv_decoded)
subprocess.run(f"{mpeg1_executable} -i {downscaled_yuv_input} -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0 -e {encoder} -h {height//2} -w {width//2}", shell=True, check=True)

print(f"Upscaling {down_format.upper()}...")
upscaled_yuv_name = f"upscaled-{name}_{og_format}_{nb_frames}f.yuv"
upscaled_yuv_input = os.path.join(cpath,upscaled_yuv_name)
subprocess.run(f'ffmpeg -s {down_res} -pix_fmt yuv420p -i {downscaled_yuv_decoded} -vf scale={width}:{height} -pix_fmt yuv420p {upscaled_yuv_input}', shell=True, check=True)

print(f"Encoding {og_format.upper()}...")
og_yuv_decoded = input_yuv_path + decode
print(og_yuv_decoded)
subprocess.run(f'{mpeg1_executable} -i "{input_yuv_path}" -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0 -e {encoder} -h {height} -w {width}', shell=True, check=True)


orig = getYValues(height,width,nb_frames,input_yuv_path)
upscaled = getYValues(height,width,nb_frames,upscaled_yuv_input)
orig_dec = getYValues(height,width,nb_frames,og_yuv_decoded)

print("Creating and encoding enhancement layer (residuals)...")

# === Residual computation ===
residual = orig - upscaled
residual_shifted = np.clip(residual + 128, 0, 255).astype(np.uint8)

# === Save residuals as raw YUV420p ===
residual_name = f"residuals-{name}_{og_format}_{nb_frames}f.yuv"
residual_file = os.path.join(cpath, residual_name)
with open(residual_file, "wb") as f:
    for i in range(nb_frames):
        y_plane = residual_shifted[:, :, i].astype(np.uint8)
        u_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)  # neutral chroma
        v_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
        f.write(y_plane.tobytes())
        f.write(u_plane.tobytes())
        f.write(v_plane.tobytes())

# === Encode residuals with your encoder ===
encoded_residual_file = residual_file + decode
subprocess.run(f"{mpeg1_executable} -i {residual_file} -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0 -e {encoder} -h {height} -w {width}" , shell=True, check=True)

# === Decode and load residuals ===
decoded_residual = getYValues(height, width, nb_frames, encoded_residual_file)
decoded_residual = decoded_residual.astype(np.float32) - 128  # Un-shift

# === Final reconstruction ===
recon = np.clip(upscaled + decoded_residual, 0, 255)


# === PSNR Comparison ===
psnr_orig = getPsnr(orig, orig_dec)
psnr_upscaled = getPsnr(orig, upscaled)
psnr_reconstructed = getPsnr(orig, recon)
for i in range(nb_frames):
    print(f'frame {i} : original = {psnr_orig[i]:.2f} dB | upscaled={psnr_upscaled[i]:.2f} dB | recon={psnr_reconstructed[i]:.2f} dB')

# === Plot results ===
displayGraph([psnr_orig, psnr_upscaled, psnr_reconstructed])
