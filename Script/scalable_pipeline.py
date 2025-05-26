
import os
import subprocess
import shutil
import cv2
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from numpy.typing import NDArray

# === parameters ===
frame_width_cif = 352
frame_height_cif = 288
frame_width_qcif = 176
frame_height_qcif = 144
nb_frames = 300
output_dir = "scalable_output"
exec_type = 'Debug'
mpeg1_executable = f"../../build/{exec_type}/ICSPCodec"
qp_value = 16
intra_period = 8

# === files ===
data_dir = "../../data"
input_yuv_cif = os.path.join(data_dir,"table.yuv")
# === folder ===
frames_cif = os.path.join(output_dir, "frames_cif")
frames_downscaled = os.path.join(output_dir, "frames_qcif")
frames_decoded = os.path.join(output_dir, "frames_qcif_decoded")
frames_upscaled = os.path.join(output_dir, "frames_qcif_upscaled")
reconstructed_dir = os.path.join(output_dir, "reconstructed")

def ensure_clean_dir(path):
    if os.path.exists(path):
        shutil.rmtree(path)
    os.makedirs(path)

# cleanup
for folder in [output_dir, frames_cif, frames_downscaled, frames_decoded, frames_upscaled, reconstructed_dir]:
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

print("Downscaling CIF...")
# I leave the right file to convert etc to you
qcif_yuv_input = os.path.join(output_dir, "base_qcif.yuv")

subprocess.run(f'ffmpeg -f rawvideo -pix_fmt yuv420p -s 352x288 -i "{input_yuv_cif}" -vf "scale=176:144" -pix_fmt yuv420p -f rawvideo "{qcif_yuv_input}"', shell=True, check=True)

print("Encoding QCIF...")
qcif_yuv_decoded = os.path.join(output_dir, "base_qcif.yuv_16_16_8_decoded.yuv")
print(qcif_yuv_decoded)
subprocess.run(f"{mpeg1_executable} -i {qcif_yuv_input} -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0", shell=True, check=True)

print("Upscaling QCIF...")
qcif_upscaled = os.path.join(output_dir,'table_upscaled.yuv')
subprocess.run(f'ffmpeg -s 176x144 -pix_fmt yuv420p -i {qcif_yuv_decoded} -vf scale=352:288 -pix_fmt yuv420p {qcif_upscaled}', shell=True, check=True)

print("Encoding CIF...")
cif_yuv_decoded = os.path.join(data_dir, "table.yuv_16_16_8_decoded.yuv")
print(cif_yuv_decoded)
subprocess.run(f"{mpeg1_executable} -i {input_yuv_cif} -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0", shell=True, check=True)


orig = getYValues(frame_height_cif,frame_width_cif,nb_frames,input_yuv_cif)
upscaled = getYValues(frame_height_cif,frame_width_cif,nb_frames,qcif_upscaled)
orig_dec = getYValues(frame_height_cif,frame_width_cif,nb_frames,cif_yuv_decoded)

print("Creating and encoding enhancement layer (residuals)...")

# === Residual computation ===
residual = orig - upscaled
residual_shifted = np.clip(residual + 128, 0, 255).astype(np.uint8)

# === Save residuals as raw YUV420p ===
residual_file = os.path.join(output_dir, "residuals.yuv")
height, width = frame_height_cif, frame_width_cif
with open(residual_file, "wb") as f:
    for i in range(nb_frames):
        y_plane = residual_shifted[:, :, i].astype(np.uint8)
        u_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)  # neutral chroma
        v_plane = np.full((height // 2, width // 2), 128, dtype=np.uint8)
        f.write(y_plane.tobytes())
        f.write(u_plane.tobytes())
        f.write(v_plane.tobytes())

# === Encode residuals with your encoder ===
encoded_residual_file = residual_file + f"_{qp_value}_{qp_value}_{intra_period}_decoded.yuv"
subprocess.run(f"{mpeg1_executable} -i {residual_file} -n {nb_frames} -q {qp_value} --intraPeriod {intra_period} --EnMultiThread 0", shell=True, check=True)

# === Decode and load residuals ===
decoded_residual = getYValues(frame_height_cif, frame_width_cif, nb_frames, encoded_residual_file)
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
