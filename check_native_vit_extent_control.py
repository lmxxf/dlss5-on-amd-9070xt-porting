"""Uniform-V original attention control beyond64 tokens; not full ViT proof."""
from pathlib import Path
import argparse,json,subprocess
p=argparse.ArgumentParser();p.add_argument('--tokens',type=int,choices=[64,128,256],required=True);a=p.parse_args()
root=Path('release/native-vit')/f'extent-control-{a.tokens}';root.mkdir(exist_ok=False)
width=8 if a.tokens==64 else 16;height=a.tokens//width;padded=(a.tokens+127)//128*128
(root/'q.fp8').write_bytes(bytes(padded*1024));(root/'k.fp8').write_bytes(bytes(padded*1024));(root/'v.fp8').write_bytes(bytes([0x38])*(padded*2048))
report={'status':'running','tokens':a.tokens,'scope':'original Q=K=0,V=1 response control; not arbitrary attention or AMD validation'}
def save():(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save()
try:
    subprocess.run(['timeout','--kill-after=2s','15s','/tmp/native-vit-attention-oracle',str(root/'q.fp8'),str(root/'k.fp8'),str(root/'v.fp8'),str(root/'output.fp8'),str(width),str(height),'32'],check=True,timeout=20)
    raw=(root/'output.fp8').read_bytes();count=a.tokens*1024
    report.update(different_from_one=sum(x!=0x38 for x in raw[:count]),nonzero_tail=sum(x!=0 for x in raw[count:]))
    assert len(raw)>=count and report['different_from_one']==0 and report['nonzero_tail']==0,'uniform attention extent control failed'
    report['status']='control_pass'
except Exception as error:
    report.update(status='fail',error=str(error));raise
finally:
    save();print(json.dumps(report,indent=2))
