"""Archive matching modified host/runtime source, including pinned dependency headers/libs."""
from pathlib import Path
import tarfile,io,subprocess
root=Path(__file__).resolve().parents[3]
host=Path(__import__('os').environ.get('RE9_UPSTREAM','/tmp/re9-upstream-bridge-review'))
out=Path('/tmp/re9-presr-source.tar.gz')
# Refresh HIP source/build recipes too: the runtime uses the current common kernels.
import shutil
for p in (root/'hip').iterdir():
 if p.is_file() and p.suffix in ('.hip','.h','.cpp','.ps1','.md'):
  shutil.copyfile(p,host/'third_party/lmxxf/hip'/p.name)
notes='''Modified RE9 OptiScaler host and lmxxf HIP runtime source.
Upstream: https://github.com/TheAutomatic/dlss-5-amd-project/tree/release/1.9.0
Commit: 8f71f73bfc836a37936e7cee6701750ad4e8bfec
Host GPL-3.0 license: OptiScaler-DLSSNR-PreSR-Multipass-main/LICENSE
Our core MIT license: third_party/lmxxf/LICENSE
The archive includes adapted sources, dependency headers/libraries and current HIP kernel sources.

Host: Visual Studio2022 v143, Windows SDK10.0.26100.0, x64 Release.
Run MSBuild on OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/OptiScaler.vcxproj.
Set SolutionDir to the absolute OptiScaler-DLSSNR-PreSR-Multipass-main directory (trailing separator).
Set PreBuildEventUseInBuild=false and PostBuildEventUseInBuild=false; generated version headers are included.
Runtime: from archive root on Linux with mingw-w64, run bash build-lmxxf-runtime.sh.
Driver HIP7 is dynamically loaded. Model weights and .hsaco are runtime assets, shipped in the binary package.
HIP recipes: third_party/lmxxf/hip/build-modules.ps1.

Pinned external dependencies:
xess 8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0
FidelityFX-SDK c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
FidelityFX-SDK-v2 60f4ea81909200d8542eca14dccb2628b763a9a3
Their upstream URLs are recorded in .gitmodules; their own licenses remain applicable.
'''
build='''#!/usr/bin/env bash
set -euo pipefail
rt=OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/lmxxf_runtime
x86_64-w64-mingw32-g++ -std=c++17 -O2 -shared -static -D_WIN32_WINNT=0x0A00 -DLMXXF_NR_RUNTIME_EXPORTS -I "$rt" -I third_party/lmxxf/src -I third_party/lmxxf/Development/HIP "$rt/LmxxfNrRuntime.cpp" -o LmxxfNrRuntime.dll -ld3d12 -ldxgi -ld3dcompiler -ldxguid
'''
prefix='OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/'
extra={'BUILD-SOURCE.txt':notes,'build-lmxxf-runtime.sh':build,prefix+'resource_build_date.h':'#define VER_BUILD_DATE "20260922_RE9_PreSR"\n',prefix+'resource_build_commit.h':'#define VER_BUILD_COMMIT "8f71f73_lmxxf_staged"\n'}
with tarfile.open(out,'w:gz') as tar:
 for p in sorted(host.rglob('*')):
  rel=p.relative_to(host)
  if not p.is_file() or any(x in ('.git','shader-cache','__pycache__') for x in rel.parts) or p.suffix.lower() in ('.exe','.dll','.pdb','.obj','.addon64','.hsaco'):continue
  if str(rel) in extra:continue
  tar.add(p,arcname=str(rel),recursive=False)
 for name,value in extra.items():
  data=value.encode();info=tarfile.TarInfo(name);info.size=len(data);info.mtime=int(__import__('time').time());info.mode=0o755 if name.endswith('.sh') else 0o644;tar.addfile(info,io.BytesIO(data))
print(out,out.stat().st_size)
