"""Measure texture transitions; hypotheses are not accepted from one threshold."""
from pathlib import Path
import subprocess,json
import numpy as np
root=Path('release/native-texture-fraction');root.mkdir(exist_ok=True);reports=[]
for index,(w,h,x) in enumerate([(120,72,10.521484375),(120,72,50.001953125),(24,16,10.501953125),(1920,1080,900.001953125)]):
    u=np.float32((x+.5)/w);bits=int(np.asarray(u).view(np.uint32));path=root/f'holdout-{index}.csv'
    if path.exists():raise FileExistsError(path)
    subprocess.run(['/tmp/probe-cuda-texture-fraction',str(w),str(h),hex(bits),'128',str(path)],check=True,timeout=15)
    data=np.genfromtxt(path,delimiter=',',names=True);values=data['u_bits'].astype(np.uint32).view(np.float32).astype(np.float64);actual=data['tex_x']
    candidates=[]
    for precision,rounding in [(20,'nearest'),(21,'floor')]:
        operation=np.rint if rounding=='nearest' else np.floor
        converted=operation(values*2**precision)/2**precision
        expected=np.rint((converted*w-.5)*256)/256
        candidates.append({'normalized_fixed_bits':precision,'rounding':rounding,'different':int(np.count_nonzero(expected!=actual))})
    reports.append({'width':w,'height':h,'center_u_bits':hex(bits),'candidates':candidates})
(root/'hypothesis-report.json').write_text(json.dumps({'scope':'independent CUDA normalized texture precision sweeps; hypotheses only','cases':reports},indent=2)+'\n');print(json.dumps(reports,indent=2))
