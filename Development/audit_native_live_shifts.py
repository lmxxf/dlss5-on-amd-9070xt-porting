"""Infer only shifts distinguishable from the saved original launch grids."""
from pathlib import Path
import json,re,struct,hashlib
root=Path('release/live-preblock-v2');raw=(root/'preblock-live-0.bin').read_bytes()
h,w=struct.unpack_from('<II',raw,0xf0);assert (h,w)==(2176,3840)
log=(root/'launches.txt').read_text();launches={}
for seq,name,x,y,z,n in re.findall(r'launch=(\d+) kernel=(\S+) grid=(\d+),(\d+),(\d+) bytes=(\d+)',log):
    launches[int(seq)]={'kernel':name,'grid':[int(x),int(y),int(z)],'bytes':int(n)}
rows=[]
for first,last,launch,divisor in [(40,47,102,32),(48,55,132,16),(56,61,140,8),(62,65,146,4),(66,69,150,2)]:
    width,height=w//divisor,h//divisor
    for block in range(first,last+1):
        seq=launch+(block-first)*(4 if first==40 else 1);record=launches[seq]
        masks=[m for m in range(4) if [(width+(4 if m&1 else 0)+7)//8,(height+(4 if m&2 else 0)+7)//8]==record['grid'][:2]]
        assert masks,'grid incompatible with assumed zero/four-pixel shift family'
        rows.append({'block':block,'launch':seq,'extent_WH':[width,height],**record,'possible_shift_masks':masks,'unique':len(masks)==1})
report={'source':'saved original RTX launch log, not a newly started game','source_sha256':hashlib.sha256((root/'launches.txt').read_bytes()).hexdigest(),
        'preblock_HW':[h,w],'assumption':'8x8 windows; per-axis offsets zero or minus four',
        'warning':'ambiguous axes require parameter blobs; this is not1080p capture','blocks':rows,
        'vit_attention_example':launches[61]}
Path('native-live-shift-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'unique':{r['block']:r['possible_shift_masks'][0] for r in rows if r['unique']},'ambiguous':[r['block'] for r in rows if not r['unique']]},indent=2))
