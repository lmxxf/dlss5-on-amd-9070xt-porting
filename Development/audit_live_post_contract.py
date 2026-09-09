"""Compare live post scalar fields with controlled oracle defaults; no equivalence claim."""
from pathlib import Path
import json,struct
root=Path('release/native-game-history-contract/backend')
data=(root/'post70.bin').read_bytes();assert len(data)==0xb8
live={'processing_hw':list(struct.unpack_from('<2I',data,0x20)),
      'origin_xy':list(struct.unpack_from('<2i',data,0x28)),
      'texture_extent_xy':list(struct.unpack_from('<2f',data,0x48)),
      'word70':struct.unpack_from('<I',data,0x70)[0]}
controlled={'processing_hw':[1152,1920],'origin_xy':[0,0],
            'texture_extent_xy':[1920.,1152.],'word70':0}
report={'scope':'scalar contract gap audit; origin affects indexing, word70 meaning not established',
        'live':live,'controlled_defaults':controlled,'different_fields':[k for k in live if live[k]!=controlled[k]]}
(root/'post-contract-audit.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
