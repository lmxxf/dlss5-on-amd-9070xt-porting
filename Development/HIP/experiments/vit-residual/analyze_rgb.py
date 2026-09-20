"""Compare actual per-frame GPU RGB, not feature proxy error."""
from pathlib import Path
import sys,json
import numpy as np
base, candidate, out = map(Path, sys.argv[1:4])
out.mkdir(parents=True, exist_ok=True)
def rgb(p):
    x=np.fromfile(p,'<f2').reshape(720,1296,4).astype(np.float32)
    assert np.isfinite(x).all(), p
    return x[:,:,:3]
def display(x):
    x=np.clip(x,0,1)
    return np.where(x<=.0031308,x*12.92,1.055*x**(1/2.4)-.055)
rows=[]
for seq in range(1,5):
    for frame in range(12):
        a=rgb(base/f'capture-900-s{seq}/rgb-frame-{frame}.f16')
        b=rgb(candidate/f'forced-4-900-s{seq}/rgb-frame-{frame}.f16')
        exact=bool(np.array_equal(a,b))
        if frame%4==0: assert exact,(seq,frame,'refresh differs')
        d=display(b)-display(a); ab=np.abs(d); mse=float(np.mean(d*d))
        rows.append(dict(sequence=seq,frame=frame,reused=frame%4!=0,exact=exact,
            display_mae_255=float(ab.mean()*255),display_p99_255=float(np.quantile(ab,.99)*255),
            display_max_255=float(ab.max()*255),psnr_db=float(-10*np.log10(mse)) if mse else None,
            raw_mae=float(np.abs(a-b).mean())))
summary=[]
for seq in range(1,5):
    r=[x for x in rows if x['sequence']==seq and x['reused']]
    summary.append(dict(sequence=seq,skipped_mae_mean=float(np.mean([x['display_mae_255'] for x in r])),
        skipped_mae_max=max(x['display_mae_255'] for x in r),
        change_frames=[x for x in r if x['frame'] in (6,7)]))
(out/'rgb-quality.json').write_text(json.dumps(dict(summary=summary,frames=rows),indent=2)+'\n')
print(json.dumps(summary,indent=2))
