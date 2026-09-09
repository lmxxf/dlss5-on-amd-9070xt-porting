#!/usr/bin/env python3
"""Apply the recovered block70 Color + neural-residual post contract."""
from __future__ import annotations
import argparse
from pathlib import Path
import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image
def main()->None:
 p=argparse.ArgumentParser();p.add_argument("ffx_color",type=Path);p.add_argument("neural",type=Path);p.add_argument("output",type=Path);p.add_argument("--intensity",type=float,default=.85);p.add_argument("--residual-limit",type=float);p.add_argument("--raw-output",type=Path);a=p.parse_args();sw,sh,rw,rh=2564,1444,2561,1441;row=sw*8;pitch=(row+255)&~255;raw=np.fromfile(a.ffx_color,np.uint8);tight=np.empty((sh,row),np.uint8)
 for y in range(sh):tight[y]=raw[y*pitch:y*pitch+row]
 color=tight.copy().view('<f2').reshape(sh,sw,4)[:rh,:rw,:3].astype(np.float32);color=np.nan_to_num(color);tensor=torch.from_numpy(color).permute(2,0,1)[None];base=F.interpolate(tensor,size=(2160,3840),mode='bilinear',align_corners=False)[0].permute(1,2,0).numpy();values=np.fromfile(a.neural,'<f4');neural_height=values.size//(3840*3);neural=values.reshape(neural_height,3840,3)[:2160];used=np.clip(neural,-a.residual_limit,a.residual_limit) if a.residual_limit else neural;result=base+used*a.intensity
 if a.raw_output:result.astype('<f4').tofile(a.raw_output)
 display=np.clip(result,0,1);Image.fromarray(np.rint(display*255).astype(np.uint8),'RGB').save(a.output);print(f"base={base.min():.7g}..{base.max():.7g} neural={neural.min():.7g}..{neural.max():.7g} result_std={result.std():.7g}")
if __name__=='__main__':main()
