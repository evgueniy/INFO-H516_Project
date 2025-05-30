import sys, os
import numpy as np
import subprocess
import re
from typing import Any
"""
File gathering all data related functions
"""

#---- YUV file info ----#

class YUV:
    """
    YUV class used for data gathering and operation
    """
    def __init__(self, path: str):
        info = YUV.extractFileInfo(path)
        # fname is the file name
        self.fname = info["fileName"]
        self.dir = info["dir"]
        # sname is the name of the sequence
        # ex: crew_4cif_600f.yuv -> crew
        self.sname = info["sequenceName"]
        self.format = info["format"]
        self.height = info["height"]
        self.width = info["width"]
        self.nframes = info["nframes"]
        self.fps = info["fps"]
        self.duration = self.nframes / self.fps

    def getPath(self) -> str:
        return os.path.join(self.dir, self.fname)

    def updatePath(self, dir: str) -> str:
        """
        updates and return new file path
        """
        self.dir = dir
        return self.getPath()

    def getFileName(self) -> str:
        return self.fname

    def getDir(self) -> str:
        return self.dir

    def getSequenceName(self):
        return self.sname

    def getFormat(self) -> str:
        return self.format

    def getHeight(self) -> int:
        return self.height

    def getWidth(self) -> int :
        return self.width

    def getNumFrames(self) -> int:
        return self.nframes

    def getFps(self) -> int:
        return self.fps

    def getDuration(self) -> float:
        return self.duration

    
    @staticmethod
    def extractFileInfo(path: str) -> dict[str, Any]:
        response = dict()
        response["fileName"] = os.path.basename(path)
        response["dir"] = os.path.dirname(path)
        res = {"qcif": (352//2, 288//2), "cif": (352, 288), "4cif": (352*2, 288*2) }
        # regex to capture name, format and nb of frames
        pattern = r"(.*)_([4|Q|q]?cif)[(]*[a-zA-Z0-9]*[)]*_([0-9]+)f" 
        match = re.search(pattern, response["fileName"])
        response["sequenceName"] = match.group(1)
        response["format"] = match.group(2)
        response["width"] = res[response["format"]][0]
        response["height"] = res[response["format"]][1]
        response["nframes"] = int(match.group(3))
        response["fps"] = 30 if response["format"] == "cif" else 60
        return response 




class ICSPCodec:
    def __init__(self, entropyCoder: str, mode: str = "Debug"):
        self.mode = mode
        self.coder = entropyCoder
        
    def getEntropyCoder(self):
        return self.coder

    def getEntropyCoderID(self):
        id = {"original": 0, "abac": 1 , "huffman": 2, "cabac": 3}
        return id[self.coder]
    
    def getICSPWorkDir(self) -> None:
        # changing to avoid building proper path in c code :) 
        # mode is either release or debug
        # used to get correct paths when executing ICSPCodec
        print("WorkDir Before change:", os.getcwd())
        os.chdir(f"../ICSPCodec/build/{self.mode}/")
        print("WorkDir After change:", os.getcwd())
    
    def run(self, yuv: YUV, qp: int, intraPeriod: int) -> str:
        """
        Runs the Codec on the given file with given iP and qp
        @return: gives the generated bin file path
        """
        yuv.updatePath("../../data")
        print(f"Generating data for qp: {qp} - inter: {intraPeriod} - encoder: {self.coder} - nframes: {yuv.getNumFrames()}"
              + f" - width: {yuv.getWidth()} - height: {yuv.getHeight()} ")
        commandArgs = [f"{os.getcwd()}/ICSPCodec", 
                       "-i", yuv.getPath(), "-n", str(yuv.getNumFrames()), 
                       "-q", str(qp), 
                       "--intraPeriod", str(intraPeriod), 
                       "--EnMultiThread", "0",
                       "-e" ,self.coder,
                       "-w" ,str(yuv.getWidth()), "-h", str(yuv.getHeight())]
        # == run the ICSPCodec on the file
        subprocess.run(commandArgs, capture_output=True, text=True, cwd=os.getcwd())
        # == verification that all went well
        csvFilename = f"../../results/{yuv.getSequenceName()}_{qp}_{qp}_{intraPeriod}_{self.getEntropyCoderID()}.csv"
        if not os.path.exists(csvFilename):
            print(f"Error: file {csvFilename} does not exist.")
            raise FileExistsError(f"Missing {csvFilename} after ICSP run.")
        binPath = f"../../results/{yuv.getSequenceName()}_comp{yuv.getFormat().upper()}_{qp}_{qp}_{intraPeriod}_{self.getEntropyCoderID()}.bin"
        if not os.path.exists(binPath):
            print(f"Error: file {csvFilename} does not exist.")
            raise FileExistsError(f"Missing {binPath} after ICSP run.")
        return binPath