from pathlib import Path
import json
p=Path(__file__).parent;t=json.loads((p/'minimal-long-statistics.json').read_text())['tiers']['original960'];reasons=[]
if t['delta_ms']>=0 or t['p99_delta_ms']>=0:reasons.append('pooled mean/p99 not both improved')
if not all(r['delta_ms']<0 and r['p99_delta_ms']<0 for r in t['rounds']):reasons.append('not all3 mean/p99 improved')
d=dict(qualified=not reasons,reasons=reasons,profile='original960',delta_us=t['delta_ms']*1000,p99_delta_us=t['p99_delta_ms']*1000,samples_per_arm=t['samples_per_arm'],scope='existing norm900 dispatch only; other runtime code identical; configuration validation still required')
(p/'minimal-decision.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps(d,indent=2))
