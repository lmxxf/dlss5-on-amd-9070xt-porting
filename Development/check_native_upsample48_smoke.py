"""Validate small original block48 call outputs; no numerical port claim."""
from pathlib import Path
import hashlib,json
root=Path('release/native-upsample48');checks=[]
for case in ('zero','main','skip'):
    raw=(root/f'{case}-output.fp8').read_bytes();active=raw[:65536]
    assert len(raw)==8*1024*1024 and not any(raw[65536:])
    nonzero=sum(x!=0 for x in active);nan=sum((x&127)==127 for x in active)
    assert nan==0 and bool(nonzero)==(case!='zero')
    checks.append({'case':case,'nonzero':nonzero,'nan':nan,'sha256':hashlib.sha256(raw).hexdigest()})
report={'status':'smoke_pass','scope':'original block48 8x8x512 to16x16x256, call/response only',
        'numerical_equivalence_verified':False,'checks':checks}
(root/'smoke-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
