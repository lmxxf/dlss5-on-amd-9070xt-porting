"""Run original ViT31..38 twice using the prepared same-source 640-token input."""
from pathlib import Path
import argparse,subprocess,os
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);args=p.parse_args()
root=args.base/'vit'
if list(root.glob('block*/trial-*.fp8')):raise RuntimeError('Existing trials: inspect before rerunning')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_VIT_')}
def run(tool,*items):subprocess.run(['/tmp/native-vit-'+tool+'-oracle',*map(str,items)],env=env,check=True,timeout=20)
for trial in (1,2):
 source=root/'input.fp8'
 for block in range(31,39):
  folder=root/f'block{block}';prefix=folder/f'trial-{trial}'
  f=lambda stage:Path(str(prefix)+'-'+stage+'.fp8')
  run('expand',source,f('expand'),folder/'expand.weights',640,160)
  run('contract',f('expand'),source,folder/'contract.weights',f('contract'),640,40)
  run('qkv',f('contract'),folder/'qkv.weights',str(prefix)+'-qkv',32,20,80)
  run('attention',f('qkv-0'),f('qkv-1'),f('qkv-2'),f('attention'),32,20,32)
  run('contract',f('attention'),f('contract'),folder/'projection.weights',f('projection'),640,40,'projection')
  source=f('projection');print('original ViT',trial,block,flush=True)
for block in range(31,39):
 for stage in ('expand','contract','qkv-0','qkv-1','qkv-2','attention','projection'):
  folder=root/f'block{block}'
  assert (folder/f'trial-1-{stage}.fp8').read_bytes()==(folder/f'trial-2-{stage}.fp8').read_bytes()
print('original ViT replay exact; CPU stage comparison still required')
