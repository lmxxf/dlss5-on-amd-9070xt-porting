"""Audit downloaded full-network results; deliberately excludes game acceptance."""
from pathlib import Path
import hashlib,json,re
import numpy as np
root=Path('release/native-temporal-network70-accepted')
log=(root/'network.stdout.log').read_text()
rows=[tuple(map(int,m)) for m in re.findall(r'network70 frame=(\d+) history=(\d+) values=(\d+) different=(\d+)',log)]
assert rows==[(i,i%2,6635520,0) for i in range(5)]
assert 'extracted_network70=exact frames=5;' in log
checks=[]
for name,oracle in [('gpu-network70.f32','release/native-rgb-valid1080/post70/oracle.f32'),
                    ('gpu-network70-temporal.f32','release/native-temporal-valid1080/post70/oracle.f32')]:
    actual=(root/name).read_bytes();expected=Path(oracle).read_bytes()
    assert len(actual)==6635520*4 and actual==expected
    assert np.isfinite(np.frombuffer(actual,np.float32)).all()
    checks.append({'file':name,'oracle':oracle,'sha256':hashlib.sha256(actual).hexdigest(),'bit_exact':True})
assert checks[0]['sha256']!=checks[1]['sha256'],'History must affect the final output for this fixture'
report={'scope':'full GPU0..70 controlled valid1080 off/on/reset; not game history feedback or display acceptance',
        'frames':rows,'outputs':checks,'pass':True,'game_verified':False}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
