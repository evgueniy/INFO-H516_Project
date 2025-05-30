import subprocess
import os
import sys

def main(file_name):
    out_dir = f"../ICSPCodec/results/mv_{file_name}_ffplay"
    if not os.path.exists(out_dir):
        os.mkdir(out_dir)
    os.chdir(out_dir)

    subprocess.run(f"ffmpeg -framerate 30 -s 352x288 -i ../../data/\"{file_name}\"_cif\\(352X288\\)_300f.yuv converted.mp4", shell=True, check=True)
    subprocess.run("ffplay -i \"converted.mp4\" -flags2 +export_mvs -vf codecview=mv=pf+bf+bb", shell=True, check=True)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("[Error] Please provide:\n\t- a video file (just the name, like `table`).")
        exit(1)

    file_name = sys.argv[1]
    main(file_name)
