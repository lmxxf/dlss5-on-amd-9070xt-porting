from pathlib import Path
import shutil,subprocess
repo=Path(__file__).resolve().parents[4];out=Path('/tmp/nr-dx12-fourstage-20261009');(out/'src').mkdir(parents=True,exist_ok=True)
for p in (repo/'src').glob('*.h'):shutil.copyfile(p,out/'src'/p.name)
p=out/'src/native_preblock_runtime.h';s=p.read_text();s=s.replace('&&wave_ffn_local&&ro','&&(wave_ffn_local||prefix_wave)&&ro');p.write_text(s) # validated private legacy prefix root selection repair
p=out/'src/native_vit_block.h';s=p.read_text();needle=' ID3D12Resource* Output()const{return projection.Output();}';assert needle in s;s=s.replace(needle,' ID3D12Resource* DiagnosticAttentionOutput()const{return attention.Output();}\n'+needle,1);p.write_text(s)
subprocess.run(['x86_64-w64-mingw32-g++','-std=c++17','-O2','-static','-w','-municode','-I',str(out/'src'),str(Path(__file__).with_name('fourstage.cpp')),'-o',str(out/'fourstage.exe'),'-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid'],check=True)
