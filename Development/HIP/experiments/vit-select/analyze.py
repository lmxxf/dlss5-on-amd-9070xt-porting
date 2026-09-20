from pathlib import Path
import sys,csv,json
import numpy as np
source=Path(sys.argv[1]);output=Path(sys.argv[2]);output.mkdir(parents=True,exist_ok=True)
base=Path(sys.argv[3]) if len(sys.argv)>3 else source
def rgb(p):return np.fromfile(p,'<f2').astype(np.float32).reshape(720,1296,4)[:,:,:3]
def display(x):
 x=np.clip(x,0,1);return np.where(x<=.0031308,x*12.92,1.055*x**(1/2.4)-.055)
result=[]
for h in [900,1080]:
 for t in ['0','0.5','1']:
  for p in [0,1,2]:
   path=source/f'select-{t}-{h}-p{p}.f16'
   if not path.exists():continue
   a=rgb(base/f'base-{h}-p{p}.f16');b=rgb(path);d=display(a)-display(b);m=float((d*d).mean())
   r=np.loadtxt(source/f'select-{t}-{h}-p{p}-route.csv',delimiter=',');n=int(r[0,0]);q=n//16;blocks=len(set(r[:,1]));saved=r[:,4].sum()*(q-2)*16/(blocks*32*q*n)
   item=dict(height=h,threshold=float(t),pattern=p,finite=bool(np.isfinite(b).all()),exact=bool(np.array_equal(a,b)),display_mae_255=float(np.abs(d).mean()*255),display_psnr_db=(-10*np.log10(m) if m else None),raw_mae=float(np.abs(a-b).mean()),raw_max=float(np.abs(a-b).max()),route_fraction=float(r[:,4].mean()),keywork_reduction=float(saved));result.append(item)
(output/'quality.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
