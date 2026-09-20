from pathlib import Path
import sys,json
import numpy as np
from common import activate,levels
outputs,data,report=map(Path,sys.argv[1:4])
a=np.fromfile(outputs/'pre.f32','<f4');b=np.fromfile(data/'oracle.f32','<f4')
assert a.shape==b.shape and np.isfinite(a).all()
q=np.fromfile(outputs/'sparse.fp8','u1');decoded=levels[q&127]*np.where(q&128,-1,1)
result=dict(preactivation_exact=bool(np.array_equal(a,b)),relative_rmse=float(np.linalg.norm(a-b)/np.linalg.norm(b)),activation_mismatches=int(np.count_nonzero(decoded!=activate(b))))
assert result['preactivation_exact'] and result['activation_mismatches']==0,result
report.write_text(json.dumps(result,indent=2)+'\n');print(result)
