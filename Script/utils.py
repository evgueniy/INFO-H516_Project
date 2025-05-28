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
        info = YUV.extractFileInfo()
        #fname is the file name
        self.fname = info["fileName"]
        self.dir = info["dir"]
        #sname is the name of the sequence
        #ex: crew_4cif_600f.yuv -> crew
        self.sname = info["sequenceName"]
        self.format = info["format"]
        self.height = info["height"]
        self.width = info["width"]
        self.nframes = info["nframes"]

    def getPath(self):
        return os.path.join(self.dir, self.fname)

    # def updatePath(self, dir: str) -> str :
        # """
        # updates and return new path
        # """
    
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
        response["nframes"] = match.group(3)
        return response 