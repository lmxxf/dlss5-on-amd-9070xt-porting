from pathlib import Path
import json,subprocess
P=Path(__file__).parent;R=P.parents[3];sums={v:k for k,v in (l.split() for l in (R/'hip/SHA256SUMS').read_text().splitlines())};res={}
for name in ['c64-wave2','c64-wave2-fast']:
 data={}
 for role in ['baseline','candidate']:
  file=P/role/'gfx1201'/(name+'.hsaco');out=P/(role+'-'+name+'.json');subprocess.run(['python3',str(P.parent/'module-inspect.py'),str(file),str(out)],check=True);data[role]=json.loads(out.read_text())
 assert data['baseline']['sha256']==sums['gfx1201/'+name+'.hsaco'],name
 b=data['baseline']['kernels'];c=data['candidate']['kernels'];new={n:v for n,v in c.items() if n not in b};changed=[n for n,v in b.items() if c.get(n)!=v];assert not changed,changed
 spill={n:v for n,v in new.items() if v['private'] or v['vgpr_spill'] or v['sgpr_spill']}
 res[name]={'new':new,'changed_existing':changed,'spill':spill,'baseline_whole_sha_matches':True};print(name,'new',len(new),'spill',list(spill))
(P/'isa-summary.json').write_text(json.dumps(res,indent=2));assert all(not r['spill'] for r in res.values())
