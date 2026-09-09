"""Match observed nearby dispatches to bounded captured shader files."""
from pathlib import Path
import collections,json,re
root=Path('release/native-game-history-contract/nearby');pid=18528
groups=collections.defaultdict(list)
for line in (root/'events.txt').read_text().splitlines():
 if not line.startswith('nearby_dispatch '):continue
 row=dict(re.findall(r'(\w+)=([^\s]+)',line))
 if row.get('pid')==str(pid):groups[row['pipeline']].append(row)
report=[]
for pipeline,events in groups.items():
 files=[p.name for prefix in ('codec','small-compute') for p in root.glob(f'{prefix}-{pid}-{pipeline}.dxbc')]
 report.append(dict(pipeline=pipeline,dispatches=len(events),first_tick=int(events[0]['tick']),
                    groups=sorted({r['groups'] for r in events}),captured_files=files))
result=dict(pid=pid,scope='bounded dispatch/shader coverage only, not history resource binding',pipelines=report)
(root/'coverage.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
