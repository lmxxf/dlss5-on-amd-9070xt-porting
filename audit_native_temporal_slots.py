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
                 'sha256':hashlib.sha256(b).hexdigest()})
r={'scope':'PID3536 first eight original preblock parameter blobs; leading slots not all proven texture handles',
   'frames':rows,'single_RGB_does_not_cover_steady_state':any(x['nonzero_leading_64bit_slots']!=rows[0]['nonzero_leading_64bit_slots'] for x in rows[1:])}
Path('native-temporal-slot-audit.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
