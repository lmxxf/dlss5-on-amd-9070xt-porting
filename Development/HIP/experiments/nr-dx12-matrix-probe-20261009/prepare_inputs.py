from pathlib import Path
import numpy as np,json,hashlib
r=Path(__file__).parent
cases=[]
def add(name,a,b,c): cases.append((name,np.asarray(a,dtype=np.float32).reshape(16,32),np.asarray(b,dtype=np.float32).reshape(32,16),np.asarray(c,dtype=np.float32).reshape(16,16)))
add('zeros',np.zeros((16,32)),np.zeros((32,16)),np.zeros((16,16)))
a=np.zeros((16,32));b=np.zeros((32,16));a[:,:16]=np.eye(16);b[:16]=np.eye(16);add('firstK_identity',a,b,np.full((16,16),.25))
a=np.zeros((16,32));b=np.zeros((32,16));a[:,16:]=np.eye(16);b[16:]=np.eye(16)[:,::-1];add('secondK_nonasym_basis',a,b,np.full((16,16),-.25))
v=np.array([-2,-1,0,.5,1,2],dtype=np.float32);i=np.arange(512);add('nonasym_dyadic',v[(i*7+i//17)%6],v[(i*5+i//13+2)%6],(np.arange(256)%7-3)*.25)
a=v[(i*3+i//9)%6];b=v[(i*11+i//7)%6];add('signed_dyadic',a,b,(np.arange(256)%9-4)*.5)
a=np.stack([t[1] for t in cases]);b=np.stack([t[2] for t in cases]);c=np.stack([t[3] for t in cases]);lookup={-2:0xc0,-1:0xb8,0:0,.5:0x30,1:0x38,2:0x40}
for tag,arr in [('a',a),('b',b)]:
 np.vectorize(lookup.__getitem__,otypes=[np.uint8])(arr).tofile(r/f'input-{tag}.fp8');arr.astype('<f2').tofile(r/f'input-{tag}.f16')
c.astype('<f4').tofile(r/'input-c.f32')
partial=(c.astype('f8')+a[:,:,:16].astype('f8')@b[:,:16,:].astype('f8')).astype('<f4');full=(c.astype('f8')+a.astype('f8')@b.astype('f8')).astype('<f4')
for tag,arr in [('partial',partial),('full',full)]:arr.tofile(r/f'gold-{tag}.f32');arr.astype('<f2').tofile(r/f'gold-{tag}.f16')
(r/'manifest.json').write_text(json.dumps({'cases':[x[0] for x in cases],'count':len(cases),'contract':'exact small dyadic FP8/F16 operands and F32 C; no overflow, all F16 gold representable; not arbitrary rounding-equivalence proof','files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in r.glob('input-*')}},indent=2)+'\n')
print('CPU_GOLD_READY cases=',len(cases))
