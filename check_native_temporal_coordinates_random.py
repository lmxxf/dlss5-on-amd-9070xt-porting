"""Original normalized temporal coordinates under spatially varying motion."""
from pathlib import Path
import json,os,struct,subprocess
import numpy as np
from native_temporal_coordinates_reference import coordinates
root=Path('release/native-temporal-coordinate-random');root.mkdir(exist_ok=False)
motion=np.zeros((8,8,4),'<f4');motion[:,:,:2]=np.random.default_rng(7304).uniform(-.75,.75,(8,8,2))
motion.tofile(root/'motion.f32')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_') and not k.startswith('DLSS5_TEMPORAL_')}
env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_SLOT8='release/native-temporal-inputs-gates/history.f32',DLSS5_PREBLOCK_SLOT10=str(root/'motion.f32'),DLSS5_TEMPORAL_DEBUG_PC='0x10d0',DLSS5_TEMPORAL_DEBUG_DIR=str(root))
run=subprocess.run(['/usr/local/cuda/bin/cuda-gdb','-batch','-x','debug_native_temporal_sample.gdb','--args','/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights','release/native-temporal-inputs-gates/rgb.f32',str(root/'main.fp8'),str(root/'down.fp8'),'0'],env=env,text=True,capture_output=True,timeout=20)
(root/'debug.log').write_text(run.stdout+run.stderr);run.check_returncode()
rows=json.loads((root/'sample-registers-10d0.json').read_text())['rows'];assert len({r['pc'] for r in rows})==1
actual=np.array([[struct.unpack('<f',struct.pack('<I',r['raw'][str(k)]))[0]*8 for k in (46,19)] for r in rows],np.float32)
expected=coordinates(motion,8,8,8,8).reshape(-1,2)[:32]
assert np.isfinite(actual).all() and np.isfinite(expected).all()
report={'scope':'original warp0 variable-motion coordinate path, slot18 absent, 8x8 only','different':int(np.count_nonzero(actual!=expected)),'max_abs':float(np.abs(actual-expected).max())}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report));assert report['different']==0
