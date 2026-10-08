#!/usr/bin/env python3
"""Encoded same-frame x/y analysis. Without --after this is a cold CPU model,
not an executed MODE3 result, a decoded HDR comparison, or current-game proof."""
import argparse,json,hashlib
from pathlib import Path
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--original',required=True);p.add_argument('--before',required=True);p.add_argument('--after');p.add_argument('--width',type=int,default=1920);p.add_argument('--valid-height',type=int,default=1080);p.add_argument('--processing-height',type=int,default=1088);p.add_argument('--strength',type=float,default=1);p.add_argument('--out',required=True);a=p.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
w,h,ph=a.width,a.valid_height,a.processing_height
x=np.fromfile(a.original,'<f4').reshape(ph,w,4)[:h,:,:3].copy();y=np.fromfile(a.before,'<f4').reshape(ph,w,3)[:h].copy()
if not (np.isfinite(x).all() and np.isfinite(y).all()):raise ValueError('invalid source batch')
if w%2 or h%2:raise ValueError('this CPU reader is restricted to even captured geometry')
r=y-x;valid=~np.all(y==0,axis=2);cw,ch=w//2,h//2
# The actual AMD capture matches toward-zero f32tof16 packing, not NumPy RNE.
# This is a scoped measured model, not a claim about every shader provider.
def half_rtz(v):
 h=v.astype(np.float16);over=np.abs(h.astype(np.float32))>np.abs(v);h[over]=np.nextafter(h[over],np.float16(0));return h.astype(np.float32)
pool=lambda v:half_rtz(v.reshape(ch,2,cw,2,3).mean((1,3),dtype=np.float32))
obs,guide=pool(r),pool(x);ok=valid.reshape(ch,2,cw,2).all((1,3));yy,xx=np.mgrid[:h,:w];ax=np.floor(xx*.5-.25);ay=np.floor(yy*.5-.25)
c0x=np.minimum(np.clip(ax,0,cw-1)*2+.5,w-1);c1x=np.minimum(np.clip(ax+1,0,cw-1)*2+.5,w-1);c0y=np.minimum(np.clip(ay,0,ch-1)*2+.5,h-1);c1y=np.minimum(np.clip(ay+1,0,ch-1)*2+.5,h-1)
fx=np.clip((xx-c0x)/np.maximum(c1x-c0x,1),0,1).astype('f4');fy=np.clip((yy-c0y)/np.maximum(c1y-c0y,1),0,1).astype('f4');mass=np.zeros((h,w),'f4');summed=np.zeros_like(r)
for k in range(4):
 qx=np.clip(ax+k%2,0,cw-1).astype(int);qy=np.clip(ay+k//2,0,ch-1).astype(int);z=np.clip((np.max(np.abs(x-guide[qy,qx]),axis=2)-.02)/.08,0,1);weight=(fx if k%2 else 1-fx)*(fy if k//2 else 1-fy)*(1-z*z*(3-2*z))*ok[qy,qx];mass+=weight;summed+=weight[...,None]*obs[qy,qx]
low=summed/np.maximum(mass[...,None],1e-12);high=r-low;extra=a.strength*(.5*low+high);peak=np.max(np.abs(extra),axis=2);excess=np.maximum(peak-.04,0);limit=.04+excess/(1+excess/.12);scale=np.where(peak>.04,limit/np.maximum(peak,1e-30),1);extra*=scale[...,None]
room=np.ones((h,w),'f4')
for c in range(3):
 d=extra[:,:,c];room=np.minimum(room,np.where(d>0,(1-y[:,:,c])/np.maximum(d,1e-30),np.where(d<0,y[:,:,c]/np.maximum(-d,1e-30),1)))
eligible=valid&(mass>=.05)&np.any(r!=0,axis=2)&np.all((y>=0)&(y<=1),axis=2)
pred=y.copy()
if a.strength>0:pred[eligible]=y[eligible]+extra[eligible]*np.maximum(room[eligible,None],0)
after=np.fromfile(a.after,'<f4').reshape(ph,w,3)[:h] if a.after else pred
if not np.isfinite(after).all():raise ValueError('invalid after batch')
d=np.abs(after-y);sha=lambda f:hashlib.sha256(Path(f).read_bytes()).hexdigest();report={'kind':'GPU_CAPTURE_SAME_NN_BEFORE_AFTER' if a.after else 'CPU_COLD_MODEL_NOT_EXECUTED_MODE3','current_live_game':False,'domain':'codec encoded RGB; not decoded HDR','width':w,'valid_height':h,'processing_height':ph,'strength':a.strength,'original_sha256':sha(a.original),'before_sha256':sha(a.before),'after_sha256':sha(a.after) if a.after else None,'changed_pixels_fraction':float(np.mean(np.any(d!=0,axis=2))),'mae':float(d.mean()),'max_abs':float(d.max()),'nn_residual_mae':float(np.abs(r).mean()),'estimated_low_support_invalid_fraction':float(np.mean(mass<.05)),'estimated_out_of_range_skip_fraction':float(np.mean(np.any((y<0)|(y>1),axis=2))),'estimated_zero_residual_fraction':float(np.mean(np.all(r==0,axis=2))),'estimated_room_zero_among_eligible':float(np.mean(room[eligible]<=0)) if eligible.any() else None,'estimated_room_below_01_among_eligible':float(np.mean(room[eligible]<.01)) if eligible.any() else None,'half_pool_rounding':'toward-zero; inferred from this AMD capture','cpu_prediction_maxabs_to_after':float(np.abs(pred-after).max()) if a.after else None,'cpu_prediction_mae_to_after':float(np.abs(pred-after).mean()) if a.after else None,'scope':'Cold spatial enhancement only; no prior history simulation. CPU gate estimates are not hardware counters.'}
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');Image.fromarray(np.round(np.clip(y,0,1)*255).astype('uint8')).save(out/'encoded-before.png');Image.fromarray(np.round(np.clip(after,0,1)*255).astype('uint8')).save(out/'encoded-after.png');Image.fromarray(np.round(np.clip(d*10,0,1)*255).astype('uint8')).save(out/'encoded-absdiff-x10.png');print(json.dumps(report,indent=2))
