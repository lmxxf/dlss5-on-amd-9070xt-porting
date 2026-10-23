from pathlib import Path
import shutil,hashlib,json
W=Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2');O=Path(__file__).parent
receipt={}
for role in ['baseline','candidate']:
 src=W/(role+'-source');dst=W/(role+'-trace-source');shutil.copytree(src,dst,dirs_exist_ok=True)
 p=dst/'Development/HIP/hip_reference_network.h';s=p.read_text();pos=s.index(' struct Timing{');s=s[:pos]+(O/'trace-fragment.inc').read_text()+s[pos:]
 s=s.replace('api.hipModuleLaunchKernel(','SmallTraceLaunch("ordered",api.hipModuleLaunchKernel,').replace('ext_launch(paired_contract_fn','SmallTraceLaunch("anyorder",ext_launch,paired_contract_fn');p.write_text(s)
 p=dst/'Development/HIP/swin_persistent_network.h';s=p.read_text().replace('api.hipModuleLaunchKernel(','SmallTraceLaunch("ordered",api.hipModuleLaunchKernel,');p.write_text(s)
 receipt[role]={str(p.relative_to(dst)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [dst/'Development/HIP/hip_reference_network.h',dst/'Development/HIP/swin_persistent_network.h']}
(O/'trace-source-identity.json').write_text(json.dumps(receipt,indent=2))
