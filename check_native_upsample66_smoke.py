"""Bounded zero/main/skip response checks of the original C32 upsample."""
from pathlib import Path
import subprocess,json
root=Path('release/native-upsample66/smoke');root.mkdir(exist_ok=False)
report={'status':'running','scope':'original block66 response only; not numerical port acceptance','checks':[]}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    for case in ('zero','main','skip'):
        (root/'input.fp8').write_bytes(bytes([0x30 if case=='main' else 0])*4096)
        (root/'skip.fp8').write_bytes(bytes([0x30 if case=='skip' else 0])*8192)
        output=root/f'{case}.fp8'
        subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-upsample-global-oracle','/tmp/dlssnr-cubins/dlssnr-00.cubin',
            'release/native-upsample66/weights.bin',str(root/'input.fp8'),str(output),str(root/f'{case}-skip-copy.fp8'),
            'cc_tinlayout_fused_swin_1h_32_1_upsample_fp8','16','16','2','2','1','10','0',str(root/'skip.fp8')],check=True,timeout=20)
        raw=output.read_bytes();active=raw[:8192]
        nonzero=sum(x!=0 for x in active);nan=sum((x&127)==127 for x in active)
        report['checks'].append({'case':case,'nonzero':nonzero,'nan':nan,'nonzero_tail':sum(x!=0 for x in raw[8192:])})
        assert len(raw)==8*1024*1024 and not any(raw[8192:]) and nan==0 and bool(nonzero)==(case!='zero')
    report['status']='smoke_pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
