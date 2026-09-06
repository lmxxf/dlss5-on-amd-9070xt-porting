"""Compare direct NVIDIA SFU trace to mathematical candidate, not a fit."""
from pathlib import Path
import json
import numpy as np
from preblock_noise_reference import fields
root=Path('release/native-rgb128/noise-residual')
t=np.fromfile(root/'trace.f32','<f4').reshape(128,128,16)
candidate=fields(128,128,0,native_steps=True)
where=np.argwhere(candidate.astype(np.float16)!=t[...,13:16].astype(np.float16))
print(json.dumps({'half_noise_differences':len(where),'positions':where[:32].tolist()}))
for y,x,g in where[:16]:
 u=t[y,x];index=0 if g==0 else 1
 lg=np.float32(np.log2(u[index]));radius=np.sqrt(np.float32(-2)*np.float32(lg*np.float32(.6931471824645996)))
 angle=np.float32(u[2+index]*np.float32(6.283185482025146))
 prod=np.float64(angle)*np.float64(np.float32(.15915493667125702))
 turns=np.float32(prod)
 if float(turns)>prod:turns=np.nextafter(turns,np.float32(0))
 trig=np.float32((np.sin if g==2 else np.cos)(np.float64(turns)*(2*np.pi)))
 native_trig=u[12 if g==2 else 10+index]
 print(json.dumps({'y':int(y),'x':int(x),'gaussian':int(g),'candidate':float(candidate[y,x,g]),'native':float(u[13+g]),'log2':[float(lg),float(u[4+index])],'sqrt':[float(radius),float(u[6+index])],'trig':[float(trig),float(native_trig)],'half_with_native_radius':float(np.float16(u[6+index]*trig)),'half_with_native_trig':float(np.float16(radius*native_trig))}))
