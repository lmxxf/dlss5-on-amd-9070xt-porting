from pathlib import Path
import subprocess,sys,shutil,re
here=Path(__file__).resolve().parent;root=here.parents[3];out=Path('/tmp/vit-adaptive-src')
subprocess.run([sys.executable,str(here.parent/'vit-residual/prepare.py')],check=True)
shutil.copytree('/tmp/vit-residual-src',out,dirs_exist_ok=True)
p=out/'kernel/deep_fast.hip';s=p.read_text()
# Only exported vit_* entry points, not DEV body helpers. Existing parameter order retained.
pattern=r'((?:WAVE|KERNEL\s+__attribute__\(\(amdgpu_flat_work_group_size\(\d+,\d+\)\)\))\s+void (vit_\w+)\()([^)]*)(\)\s*\{)'
names=[]
def patch(m):
    if m[2].startswith('vit_residual_'):return m[0]
    names.append(m[2]);return m[1]+m[3]+',const uint*reuse_gate'+m[4]+'if(reuse_gate&&reuse_gate[0])return;'
s=re.sub(pattern,patch,s);assert 'vit_pack_input' in names and 'vit_attention_fused_400_bytein' in names
p.write_text(s+'\n'+(here/'gate.hip').read_text())
(here/'guarded-kernels.txt').write_text('\n'.join(sorted(set(names)))+'\n')
p=out/'Development/HIP/hip_reference_network.h';s=p.read_text()
s=s.replace('source=ResidualVitGroup(source,n);','source=AdaptiveVitGroup(source,n);')
s=s.replace(' Tensor residual_anchor_in,', (here/'wrapper.inc').read_text()+'\n Tensor residual_anchor_in,',1)
s=s.replace('void*argv[]={static_cast<void*>(&args)...};','void*reuse_gate_arg=adaptive_active?P(adaptive_state):nullptr;void*argv[]={static_cast<void*>(&args)...,static_cast<void*>(&reuse_gate_arg)};',1)
s=s.replace('device_noise.reset();gather_maps[0].clear();','adaptive_anchor_in.reset();adaptive_anchor_out.reset();adaptive_gain.reset();adaptive_stats.reset();adaptive_state.reset();device_noise.reset();gather_maps[0].clear();',1)
p.write_text(s)
# Additional held-out controlled sequences; modulo12 makes dynamic timing repeatable.
p=out/'Development/HIP/benchmark_live_capture.cpp';s=p.read_text().replace('sequence>4||(sequence&&N>32)','sequence>7')
s=s.replace('if(sequence){void*mp=', 'UINT step=i%12; if(sequence){void*mp=')
s=s.replace('(x+W-i%W)%W','(x+W-step)%W').replace('sequence==3&&i>=N/2','sequence==3&&step>=6').replace('(1.f+.01f*i)','(1.f+.01f*step)').replace('sequence==4&&i>=N/2','sequence==4&&step>=6')
s=s.replace('uint16_t rgba[4];memcpy(rgba,frozen.data()+(size_t(y)*W+sx)*4,8);', '''UINT sy=sequence==5?(y+H-2*step)%H:y;
  if(sequence==7&&((step/3)&1))sx=W-1-x;
  uint16_t rgba[4];memcpy(rgba,frozen.data()+(size_t(sy)*W+sx)*4,8);
  if(sequence==5)for(unsigned c=0;c<3;c++)rgba[c]=Float16ForSequence(NativeHalfToFloat(rgba[c])*(1.f+.005f*(step<6?step:12-step)));
  if(sequence==6&&step>=4&&step<9&&x>=W/3&&x<W/3+W/10&&y>=H/3&&y<H/3+H/10)for(unsigned c=0;c<3;c++)rgba[c]=0;''')
p.write_text(s)
print(out, 'guarded',len(set(names)))
