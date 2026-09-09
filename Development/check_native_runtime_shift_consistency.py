"""Check C++ shift table and regenerated reference against captured blobs."""
from pathlib import Path
import json,re
capture=json.loads(Path('native-runtime-parameters.json').read_text())
expected={r['block']:r['shift_mask'] for r in capture['decoder']}
header=Path('native_runtime_shifts.h').read_text().split('masks[]={',1)[1].split('};',1)[0]
header=re.sub(r'//[^\n]*','',header);values=[int(x) for x in re.findall(r'\d+',header)]
assert len(values)==30 and values==[expected[i] for i in range(40,70)]
root=Path('release/native-runtime-rgb512');checks=[]
for i in range(40,70):
    directory=root/(f'upsample{i}-shift{expected[i]}' if i in (48,56,62,66) else f'decoder-block{i}')
    report=json.loads((directory/'validation.json').read_text())
    assert report['status']=='pass' and report['shift']==expected[i]
    assert all(r['different']==0 for r in report['checks']) if 'checks' in report else report['different']==0
    checks.append(i)
assert json.loads((root/'post70/validation.json').read_text())['status']=='pass'
report={'status':'pass','checked_blocks':checks,'scope':'captured shifts match C++ table and regenerated CPU/original references; GPU pending'}
(root/'shift-consistency.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
