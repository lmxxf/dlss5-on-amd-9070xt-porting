from pathlib import Path
import importlib.util,json,hashlib,numpy as np
p=Path(__file__).parent
s=importlib.util.spec_from_file_location('recipe','Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
source=next(x[2] for x in m.recipe(Path('hip')) if x[0]=='deep_fast-packed-fast')
old='''  for(uint e=0;e<8;e++){
   float af=clampf(a[e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   uint hb=(bits(af)>>13)-0x1c000u;unsigned short u=(unsigned short)(((hb<<4)+0x4000u)&65535u);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;x[e]=cv.h;'''
new='''  uint pair_exp[4];
  for(uint e=0;e<4;e++){
   float af0=clampf(a[2*e]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   float af1=clampf(a[2*e+1]*from_half(0x2dbb)+1.708984375f,1.439453125f,1.9775390625f);
   pair_exp[e]=(__builtin_amdgcn_perm(bits(af1),bits(af0),0x06050201u)>>1)&0x3ff03ff0u;
  }
  x=__builtin_bit_cast(h8,pair_exp);
  for(uint e=0;e<8;e++){
   unsigned short u=__builtin_bit_cast(unsigned short,x[e]);
   union{unsigned short u;_Float16 h;}cv;cv.u=u;'''
assert source.count(old)==2
(p/'baseline.hip').write_text(source);(p/'candidate.hip').write_text(source.replace(old,new,1))
checks={}
for name,lo,hi,shift,add,mask in [('swin',1.03125,1.5693359375,5,0x8000,0x7fe0),('vit',1.439453125,1.9775390625,4,0x4000,0x3ff0)]:
 l=np.float32(lo).view(np.uint32).item();h=np.float32(hi).view(np.uint32).item();u=np.arange(l,h+1,dtype=np.uint32)
 oldbits=((((u>>13)-np.uint32(0x1c000))<<shift)+np.uint32(add))&65535
 newbits=(u>>(13-shift))&mask
 assert np.array_equal(oldbits,newbits)
 # Reversed pair covers every value once in each position; pair operation is separable.
 v=u[::-1];packed=((u>>8)&65535)|(((v>>8)&65535)<<16)
 cand=(packed if name=='swin' else packed>>1)&(mask|(mask<<16))
 assert np.array_equal(cand,oldbits|(oldbits[::-1]<<16))
 checks[name]={'low':hex(l),'high':hex(h),'patterns':len(u),'scalar_diff':0,'paired_diff':0}
(p/'cpu-proof.json').write_text(json.dumps(checks,indent=2))
r=r'D:\DLSSNR-Lab\yami-round4-review-20261031';compiler=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe'
files=[{'path':compiler,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}]
for a in ['baseline','candidate']:files.append({'path':r+'\\'+a+'.hip','sha256':hashlib.sha256((p/(a+'.hip')).read_bytes()).hexdigest()})
jobs=[{'label':'compile/'+a,'exe':compiler,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\deep_fast-packed-fast.hsaco" "'+r+'\\'+a+'.hip" comgr gfx1201'} for a in ['baseline','candidate']]
(p/'compile.json').write_text(json.dumps({'name':'vit-pair','files':files,'jobs':jobs},indent=2))
runner=Path('Development/results/ksearch-round4-20261031/run-jobs.ps1').read_text().replace('D:\\DLSSNR-Lab\\ksearch-round4-20261031',r)
(p/'run-jobs.ps1').write_text(runner)
print(checks)
