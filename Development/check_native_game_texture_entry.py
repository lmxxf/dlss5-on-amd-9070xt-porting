"""Join observed SRV creations at merged-call entry, never skip null descriptors."""
from pathlib import Path
import argparse,json,re,struct
p=argparse.ArgumentParser();p.add_argument('--pid',required=True,type=int);args=p.parse_args()
root=Path('release/native-game-history-contract/entry')
latest={};entries={};handles={}
for line in (root/'native-nvapi-texture-contract.txt').read_text().splitlines():
    row=dict(re.findall(r'(\w+)=([^\s]+)',line))
    if row.get('pid')!=str(args.pid):continue
    if line.startswith('srv_create'):
        latest[(row['device'],row['desc'])]=row
    elif line.startswith('merged_enter'):
        entries[row['call']]={'entry':row,'srv':latest.get((row['device'],row['texture_desc']))}
    elif 'merged_handle' in row and row.get('status')=='0':
        handles[int(row['merged_handle'],16)]={'return':row,'entry_observation':entries.get(row['call'])}
meta=(root/'preblock-live-parameters.txt').read_text()
assert re.search(r'capture=1 pid='+str(args.pid)+r'\b',meta),'Matching preblock capture metadata absent'
blob=(root/'preblock-live-1.bin').read_bytes();assert len(blob)==0x108
report={'scope':'observed descriptor state at merged entry; not image content/history provenance proof','pid':args.pid,'slots':[]}
for offset in (0,8,16):
    handle=struct.unpack_from('<Q',blob,offset)[0]
    report['slots'].append({'offset':hex(offset),'handle':hex(handle),'observation':handles.get(handle)})
(root/'entry-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
