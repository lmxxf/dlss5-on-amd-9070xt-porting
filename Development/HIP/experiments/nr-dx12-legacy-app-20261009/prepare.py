from pathlib import Path
import shutil,subprocess
repo=Path(__file__).resolve().parents[4];out=Path('/tmp/nr-dx12-legacy-app-20261009')
for sub in ['src','Development/HIP']:
 (out/sub).mkdir(parents=True,exist_ok=True)
 for p in (repo/sub).glob('*.h'):shutil.copyfile(p,out/sub/p.name)
p=out/'src/native_preblock_runtime.h';s=p.read_text();s=s.replace('&&wave_ffn_local&&ro','&&(wave_ffn_local||prefix_wave)&&ro');p.write_text(s) # validated private legacy prefix root selection repair
p=out/'src/native_game_frame.h';s=p.read_text();needle='    if(r.low_temporal.enabled)r.low_temporal.Record(c,r.input.PostBase(),r.network.Output(),r.network.DirectInput()?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);';assert needle in s;s=s.replace(needle,'#ifdef DLSS5_USE_HIP\n'+needle+'\n#else\n    if(r.low_temporal.enabled)throw std::runtime_error("legacy HLSL diagnostic requires temporal off");\n#endif',1);p.write_text(s)
s=(repo/'Development/HIP/benchmark_vit_reuse.cpp').read_text();needle='std::wstring assets=argv[1],prefix=argv[4];';assert needle in s;s=s.replace(needle,needle+'{const auto g=NativeCurrentNetworkGeometry();printf("LEGACY_GEOMETRY processing=%ux%u valid=%ux%u Network70_VitTokensArgument=%u\\n",g.processing_width,g.processing_height,g.valid_width,g.valid_height,g.VitTokens());}',1);(out/'benchmark.cpp').write_text(s)
subprocess.run(['x86_64-w64-mingw32-g++','-std=c++17','-O2','-static','-w','-municode','-DDLSS5_COMPARE_HLSL=1','-I',str(out/'src'),'-I',str(out/'Development/HIP'),str(out/'benchmark.cpp'),'-o',str(out/'benchmark.exe'),'-ld3d12','-ldxgi','-ld3dcompiler','-ldxguid'],check=True)
