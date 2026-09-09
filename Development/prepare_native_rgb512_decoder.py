"""Original RGB512->ViT38->repack->decoder39 with CPU/layout cross-checks."""
from pathlib import Path
import json,subprocess
import numpy as np
from native_split_reference import bits
from native_decoder_entry_reference import unpack,decoder_entry
from decode_tinlayout_global import e4m3fn
root=Path('release/native-rgb512');n=65536
repack=np.fromfile(root/'repack-inverse-output-to-input.i32','<u4')
forward=np.fromfile(root/'repack-output-to-input.i32','<u4')
assert repack.size==forward.size==n and np.array_equal(repack,np.argsort(forward))
meta=json.loads((root/'repack-inverse.json').read_text())
assert meta['actual_input_verified'] and meta['held_out_random_inputs']==2
job=Path(json.loads((root/'bridge.json').read_text())['original_vit_chain'])
assert json.loads((job/'validation.json').read_text())['status']=='pass'
inverse=np.argsort(np.load('release/native-c512/split-view/mapping.npz')['cell_output_to_hwc'])
def hwc1024(raw):
    return raw.reshape(2,2,2,8192)[:,:,:,inverse].reshape(2,2,2,4,4,512).transpose(0,3,1,4,2,5).reshape(8,8,1024)
def hwc512(raw):
    return raw.reshape(16,8192)[:,inverse].reshape(4,4,4,4,512).transpose(0,2,1,3,4).reshape(16,16,512)
mainraw=np.fromfile(root/'block38-2d.fp8',np.uint8)
source=np.fromfile(job/'block38/projection.fp8',np.uint8)
assert mainraw.size==n and np.array_equal(mainraw,source[repack])
t=bits(n,[2,6,7,8,14,15]);c=bits(n,[0,1,3,4,5,9,10,11,12,13])
gather=hwc1024((t*1024+c)[repack]).astype('<u4')
assert np.unique(gather).size==n and gather.max()<n
logical=np.empty(n,np.float32);logical[t*1024+c]=e4m3fn(source[:n])
main=e4m3fn(hwc1024(mainraw))
assert np.array_equal(logical[gather],main)
skipraw=np.fromfile(root/'block30-pool-main.fp8',np.uint8)
assert not np.any(skipraw[131072:])
skip=e4m3fn(hwc512(skipraw[:131072]))
skipraw[:131072].tofile(root/'decoder39-skip.fp8')
weights=root/'decoder39.weights'
subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll','block39.layer0.layer',str(weights)],check=True,timeout=5)
prefix=root/'decoder39-original'
subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-decoder-entry-oracle',
    '/tmp/dlssnr-cubins/dlssnr-06.cubin',str(root/'block38-2d.fp8'),str(root/'decoder39-skip.fp8'),
    str(weights),str(prefix),'8','8','--run'],check=True,timeout=20)
raw=np.fromfile(str(prefix)+'.output.fp8',np.uint8)
assert not np.any(raw[131072:]) and not np.any((raw[:131072]&127)==127)
target=e4m3fn(hwc512(raw[:131072]));params=unpack(weights)
got=decoder_entry(main,skip,params);different=int(np.count_nonzero(got!=target))
report={'status':'pass' if different==0 else 'fail','values':int(target.size),'different':different,
        'max_error':float(np.max(np.abs(got-target))),
        'scope':'original RGB512 through decoder39 with block30 main as skip; AMD chain pending'}
(root/'decoder39-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
assert different==0,'RGB-derived decoder differs'
gather.tofile(root/'vit-to-decoder.i32');target.tofile(root/'decoder39-oracle.f32')
np.concatenate([params[0].ravel(),params[1]]).astype('<f4').tofile(root/'decoder39-weights.f32')
