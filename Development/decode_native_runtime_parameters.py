"""Decode pointer-free geometry/mode evidence from original CPU parameter blobs."""
from pathlib import Path
import json,re,struct,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('release/native-kernel-params-24064-11278468'));p.add_argument('--output',type=Path,default=Path('native-runtime-parameters.json'));p.add_argument('--scope',default='original startup at4K; no new1080p/equipment frame claim');args=p.parse_args()
root=args.root;log=(root/'launches.txt').read_text()
assert 'format=indirect-v4' in log
launches={int(s):{'kernel':k,'grid':[int(x),int(y),int(z)],'bytes':int(n)} for s,k,x,y,z,n in re.findall(r'launch=(\d+) kernel=(\S+) grid=(\d+),(\d+),(\d+) bytes=(\d+)',log)}
def blob(seq):
    raw=(root/f'launch-{seq:04d}.bin').read_bytes();assert len(raw)==launches[seq]['bytes'];return raw
rows=[]
for first,last,start,stride in [(1,4,2,1),(5,8,6,1),(9,14,10,1),(15,22,16,1),(23,30,26,4),(40,47,102,4),(48,55,132,1),(56,61,140,1),(62,65,146,1),(66,69,150,1)]:
    for index in range(first,last+1):
        seq=start+(index-first)*stride;r=blob(seq)
        dim_offset,shift_offset=(24,32) if index<=4 or 23<=index<=47 or index>=66 else (32,40)
        h,w=struct.unpack_from('<ii',r,dim_offset);x,y=struct.unpack_from('<ii',r,shift_offset)
        assert h>0 and w>0 and x in (0,-4) and y in (0,-4)
        rows.append({'block':index,'launch':seq,**launches[seq],'HW':[h,w],'offset_XY':[x,y],'shift_mask':int(x==-4)+2*int(y==-4),'blob_sha256':hashlib.sha256(r).hexdigest()})
vit=[]
for seq,offset in [(57,16),(58,64),(59,64),(60,72),(61,56),(62,64),(98,16)]:
    r=blob(seq);vit.append({'launch':seq,**launches[seq],'HW_pair':list(struct.unpack_from('<ii',r,offset)),'field_offset':offset})
r=blob(154)
post={'HW':list(struct.unpack_from('<ii',r,32)),'input_scale':struct.unpack_from('<f',r,48)[0],'rgb_mode':struct.unpack_from('<I',r,52)[0],
      'texture_presence':{hex(off):bool(struct.unpack_from('<Q',r,off)[0]) for off in (0x38,0x58,0x60)}}
report={'source':str(root),'capture_scope':args.scope,'pointer_values_omitted':True,'encoder':[r for r in rows if r['block']<=30],'decoder':[r for r in rows if r['block']>=40],'vit':vit,'post':post}
args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'encoder_shifts':{r['block']:r['shift_mask'] for r in report['encoder']},'decoder_shifts':{r['block']:r['shift_mask'] for r in report['decoder']},'vit':vit,'post':post},indent=2))
