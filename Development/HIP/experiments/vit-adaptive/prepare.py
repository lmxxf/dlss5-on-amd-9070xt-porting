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
s=s.replace('class Network {', 'inline std::atomic<int> AdaptivePreviewState{0};\nclass Network {',1)
s=s.replace('#include <chrono>', '#include <chrono>\n#include <atomic>',1)
s=s.replace('Tensor RunGraph(Tensor color,Tensor noisegpu,Tensor hist){','Tensor RunGraph(Tensor color,Tensor noisegpu,Tensor hist){adaptive_image=P(color);',1)
s=s.replace('source=ResidualVitGroup(source,n);','source=AdaptiveVitGroup(source,n);')
s=s.replace(' Tensor residual_anchor_in,', (here/'wrapper.inc').read_text()+'\n Tensor residual_anchor_in,',1)
s=s.replace('void*argv[]={static_cast<void*>(&args)...};','void*reuse_gate_arg=adaptive_active?P(adaptive_state):nullptr;void*argv[]={static_cast<void*>(&args)...,static_cast<void*>(&reuse_gate_arg)};',1)
s=s.replace('device_noise.reset();gather_maps[0].clear();','adaptive_image_anchor.reset();adaptive_image_signature.reset();adaptive_image_delta.reset();adaptive_anchor_in.reset();adaptive_anchor_out.reset();adaptive_gain.reset();adaptive_stats.reset();adaptive_state.reset();device_noise.reset();gather_maps[0].clear();',1)
s=s.replace('void Enqueue(void*rgba,void*history,void*rgb_output,U seed){','void Enqueue(void*rgba,void*history,void*rgb_output,U seed){if(adaptive_prev_history!=history||adaptive_prev_seed!=seed||adaptive_prev_input!=rgba){adaptive_dirty=true;adaptive_prev_history=history;adaptive_prev_seed=seed;adaptive_prev_input=rgba;}',1)
p.write_text(s)
# Additional stress inputs, used during tuning (not held-out validation); repeat every12 frames.
p=out/'Development/HIP/benchmark_live_capture.cpp';s=p.read_text().replace('sequence>4||(sequence&&N>32)','sequence>7')
s=s.replace('if(sequence){void*mp=', 'UINT step=i%12; if(sequence){void*mp=')
s=s.replace('(x+W-i%W)%W','(x+W-step)%W').replace('sequence==3&&i>=N/2','sequence==3&&step>=6').replace('(1.f+.01f*i)','(1.f+.01f*step)').replace('sequence==4&&i>=N/2','sequence==4&&step>=6')
s=s.replace('uint16_t rgba[4];memcpy(rgba,frozen.data()+(size_t(y)*W+sx)*4,8);', '''UINT sy=sequence==5?(y+H-2*step)%H:y;
  if(sequence==7&&((step/3)&1))sx=W-1-x;
  uint16_t rgba[4];memcpy(rgba,frozen.data()+(size_t(sy)*W+sx)*4,8);
  if(sequence==5)for(unsigned c=0;c<3;c++)rgba[c]=Float16ForSequence(NativeHalfToFloat(rgba[c])*(1.f+.005f*(step<6?step:12-step)));
  if(sequence==6&&step>=4&&step<9&&x>=W/3&&x<W/3+W/10&&y>=H/3&&y<H/3+H/10)for(unsigned c=0;c<3;c++)rgba[c]=0;''')
# Precompute controlled sequences once: CPU half conversion must not starve/clock down the GPU between timed frames.
start=s.index('UINT step=i%12; if(sequence){void*mp=')
end=s.index('// Each step restores',start)
body=s[s.index('  UINT sx=',start):s.index('  memcpy(static_cast<char*>(mp)',start)]
pre='std::vector<std::vector<uint16_t>> sequence_frames; if(sequence){sequence_frames.resize(12);for(UINT step=0;step<12;step++){sequence_frames[step].resize(count);for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){\n'+body+'memcpy(sequence_frames[step].data()+(size_t(y)*W+x)*4,rgba,8);}}}\n'
s=s[:start]+'if(sequence){void*mp=nullptr;ck(up->Map(0,&none,&mp));for(UINT y=0;y<H;y++)memcpy(static_cast<char*>(mp)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,sequence_frames[i%12].data()+size_t(y)*W*4,size_t(W)*8);up->Unmap(0,nullptr); }\n '+s[end:]
s=s.replace('for(UINT i=0;i<N;i++){',pre+'for(UINT i=0;i<N;i++){',1)
p.write_text(s)
print(out, 'guarded' ,len(set(names)))

shutil.copytree(root/'scripts',out/'scripts',dirs_exist_ok=True)
p=out/'src/native_game_frame.h';s=p.read_text();needle='r.fps_tick=now;';assert needle in s
s=s.replace(needle, '\n#ifdef DLSS5_USE_HIP\n  if(const char*v=std::getenv("DLSS5_VIT_REUSE_HOTKEY");v&&!strcmp(v,"1")){size_t used=strlen(r.fps_text);snprintf(r.fps_text+used,sizeof(r.fps_text)-used," %s",hip_reference::AdaptivePreviewState.load()?"AE":"EXACT");}\n#endif\n  '+needle,1);p.write_text(s)
