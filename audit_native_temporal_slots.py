"""Pointer-free first-eight-frame preblock presence audit; slots not yet typed."""
from pathlib import Path
import hashlib,json,struct
root=Path('release/native-game-present/temporal-3536')
rows=[]
for i in range(8):
    b=(root/f'preblock-live-{i}.bin').read_bytes()
    assert len(b)==0x108
    rows.append({'frame':i,'seed':struct.unpack_from('<I',b,0xc8)[0],
                 'HW':list(struct.unpack_from('<II',b,0xf0)),
                 'nonzero_leading_64bit_slots':[hex(o) for o in range(0,0x48,8) if struct.unpack_from('<Q',b,o)[0]],
                 'nonzero_texture_slots':[hex(o) for o in (0,8,0x10,0x18,0x20) if struct.unpack_from('<Q',b,o)[0]],
                 'secondary_extent_xy':list(struct.unpack_from('<ff',b,0x30)),
                 'secondary_inverse_extent_xy':list(struct.unpack_from('<ff',b,0x38)),
                 'sha256':hashlib.sha256(b).hexdigest()})
r={'scope':'PID3536 first eight preblock blobs; texture slots identified by original TEX operand dataflow, not semantic resource identities',
   'texture_fetch_evidence':{'0x0':{'load_pc':'0x580','tex_pc':'0x620','channel_mask':'0x7'},'0x8':{'load_pc':'0x10a0','tex_pc':'0x1590','channel_mask':'0x7'},'0x10':{'load_pc':'0xbf0','tex_pc':'0x1090','channel_mask':'0x3'},'0x18':{'load_pc':'0xc10','tex_pc':'0xd10','channel_mask':'0x1'},'0x20':{'load_pc':'0x19b0','tex_pc':'0x19f0','channel_mask':'0x6'}},
   'nontexture_correction':'0x30/0x38 are float2 extents/reciprocals, used by FFMA/FMUL; not resource handles',
   'frames':rows,'single_RGB_does_not_cover_steady_state':any(x['nonzero_leading_64bit_slots']!=rows[0]['nonzero_leading_64bit_slots'] for x in rows[1:])}
Path('native-temporal-slot-audit.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
