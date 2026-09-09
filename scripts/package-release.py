#!/usr/bin/env python3
"""Build the user package for the game addon.

usage: package-release.py <assets dir (copy of native-game-tiled-assets)> <addon64> <d3d12.dll (ReShade)> <D3D12Core dir> <flags.txt> <noise.f32> <out dir> <version>

Layout (drop the whole folder's contents into SB\\Binaries\\Win64):
  d3d12.dll                      ReShade loader
  dlss5-amd.addon64              the addon (finds DLSS5-AMD\\ next to itself)
  DLSS5-D3D12-721\\               D3D12 Agility SDK 1.721 preview runtime (Shader Model 6.10)
  DLSS5-AMD\\native-game-flags.txt, enable-game-sdk721.txt, continuous-every-frame.txt, temporal-history.txt, logs\\
  DLSS5-AMD\\native-game-tiled-assets\\  weights (f16 where exact, f32 otherwise), compiled shaders, runtime shaders, noise.f32
"""
import sys,os,shutil,hashlib,numpy as np
assets,addon,loader,core,flags,noise,out,version=sys.argv[1:9]
root=os.path.join(out,f'DLSS5-AMD-{version}');lab=os.path.join(root,'DLSS5-AMD');tiled=os.path.join(lab,'native-game-tiled-assets')
if os.path.exists(root):shutil.rmtree(root)
os.makedirs(tiled);os.makedirs(os.path.join(lab,'logs'))
open(os.path.join(lab,'logs','.keep'),'w').close()
shutil.copy(loader,os.path.join(root,'d3d12.dll'));shutil.copy(addon,os.path.join(root,'dlss5-amd.addon64'))
shutil.copytree(core,os.path.join(root,'DLSS5-D3D12-721'))
lines=[l for l in open(flags,encoding='utf-8').read().splitlines() if not l.startswith(('DLSS5_RESERVE_VRAM_MB','DLSS5_DEBUG_DUMPS'))]  # no-op reservation; diagnostic dumps
open(os.path.join(lab,'native-game-flags.txt'),'w',encoding='utf-8',newline='\n').write('\n'.join(lines)+'\n')
for name,text in [('enable-game-sdk721.txt','sdk721\n'),('continuous-every-frame.txt','every-frame\n'),('temporal-history.txt','temporal\n')]:
    open(os.path.join(lab,name),'w',newline='\n').write(text)
kept_f32=[];halves=0;total=0
FIXTURES=('gpu-network70','history','input','motion','oracle-')  # bench fixtures that live in the dev asset dir
for f in sorted(os.listdir(assets)):
    ext=os.path.splitext(f)[1]
    if any(f.startswith(x) for x in FIXTURES):continue
    src=os.path.join(assets,f)
    if ext in('.cso','.hlsl','.hlsli','.i32'):shutil.copy(src,os.path.join(tiled,f))
    elif ext=='.f32':
        a=np.fromfile(src,np.float32);total+=a.nbytes
        h=a.astype(np.float16)
        if np.array_equal(h.astype(np.float32),a):h.tofile(os.path.join(tiled,f[:-4]+'.f16'));halves+=h.nbytes
        else:shutil.copy(src,os.path.join(tiled,f));kept_f32.append(f);halves+=a.nbytes
shutil.copy(noise,os.path.join(tiled,'noise.f32'))
print('weights',round(total/2**20),'MB ->',round(halves/2**20),'MB; kept f32:',kept_f32)
here=os.path.dirname(os.path.abspath(__file__));shutil.copy(os.path.join(here,'package-README.txt'),os.path.join(root,'README.txt'))
for lic in('ReShade-LICENSE.txt','MinHook-LICENSE.txt'):
    src=os.path.join(os.path.dirname(loader),lic)
    if os.path.exists(src):shutil.copy(src,os.path.join(root,lic))
sums=[]
for d,_,fs in os.walk(root):
    for f in sorted(fs):
        p=os.path.join(d,f);rel=os.path.relpath(p,root).replace(os.sep,'\\')
        sums.append(hashlib.sha256(open(p,'rb').read()).hexdigest()+'  '+rel)
open(os.path.join(root,'SHA256SUMS.txt'),'w',newline='\n').write('\n'.join(sums)+'\n')
print('package at',root)
