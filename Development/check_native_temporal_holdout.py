"""Independent shifts for the original temporal sampler; report mismatches."""
from pathlib import Path
import os,json,subprocess
import numpy as np
from native_temporal_sampling_reference import geometry,bilinear
root=Path('release/native-temporal-holdout');root.mkdir(exist_ok=False)
rng=np.random.default_rng(7303);rgb=rng.uniform(.1,.9,(8,8,4)).astype('<f4');rgb[:,:,3]=1
history=rng.uniform(.1,.9,(8,8,4)).astype('<f4');history[:,:,3]=1
rgb.tofile(root/'rgb.f32');history.tofile(root/'history.f32')
results=[]
for case,(dx,dy) in enumerate([(.37,-.29),(-.63,.81),(.015625,.484375),(.72,.23)]):
    folder=root/f'case{case}';folder.mkdir()
    motion=np.zeros((8,8,4),'<f4');motion[:,:,0]=dx;motion[:,:,1]=dy;motion.tofile(folder/'motion.f32')
    env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_PREBLOCK_') and not k.startswith('DLSS5_TEMPORAL_')}
    env.update(DLSS5_PREBLOCK_PARAMETER_FILE='release/native-game-present/temporal-3536/preblock-live-1.bin',DLSS5_PREBLOCK_SEED='0',DLSS5_PREBLOCK_SLOT8=str(root/'history.f32'),DLSS5_PREBLOCK_SLOT10=str(folder/'motion.f32'),DLSS5_TEMPORAL_DEBUG_DIR=str(folder))
    run=subprocess.run(['/usr/local/cuda/bin/cuda-gdb','-batch','-x','debug_native_temporal_sample.gdb','--args','/tmp/native-preblock-temporal-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin','release/native-rgb-game/block0.weights',str(root/'rgb.f32'),str(folder/'main.fp8'),str(folder/'down.fp8'),'0'],env=env,capture_output=True,text=True,timeout=20)
    (folder/'debug.log').write_text(run.stdout+run.stderr);run.check_returncode()
    rows=json.loads((folder/'sample-registers.json').read_text())['rows'];assert len(rows)==32 and len({r['pc'] for r in rows})==1
    actual=np.array([r['rgb'] for r in rows],np.float32);assert np.isfinite(actual).all()
    lane=np.arange(32);xy,w=geometry(lane%8+.5+float(np.float32(dx)),lane//8+.5+float(np.float32(dy)),8,8)
    checks=[]
    for product_bits in (8,9,10,None):
        expected=(bilinear(history[:,:,:3],xy,8,product_bits)*w[...,None]).sum(-2)
        checks.append({'product_bits':product_bits,'different_half':int(np.count_nonzero(expected.astype(np.float16)!=actual.astype(np.float16))),'max_abs':float(np.abs(expected-actual).max())})
    results.append({'shift_xy':[dx,dy],'checks':checks});print(results[-1],flush=True)
    total=sum(r['checks'][0]['different_half'] for r in results)
    (root/'validation.json').write_text(json.dumps({'scope':'holdout original warp0 sampling, not AMD/full temporal path','cases':results,'fraction8_product8_half_differences':total,'exact_half_on_all_completed_cases':total==0},indent=2)+'\n')
