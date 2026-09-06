"""Run a complete original 64-token block31, checking every CPU reference stage."""
from pathlib import Path
import argparse,subprocess,json,os,hashlib
import numpy as np
from native_split_reference import bits
from native_c32_reference import F
from native_vit_linear_reference import unpack_expand,expand,unpack_residual,residual_projection
from native_vit_qkv_reference import unpack as unpack_qkv,qkv
from native_vit_attention_reference import attention
from decode_tinlayout_global import e4m3fn
from encode_tinlayout_global import quantize
parser=argparse.ArgumentParser();parser.add_argument('--seed',type=int,default=2101);parser.add_argument('--last-block',type=int,choices=range(31,39),default=31);parser.add_argument('--input1d',type=Path);args=parser.parse_args()
input_sha=hashlib.sha256(args.input1d.read_bytes()).hexdigest() if args.input1d else None;fixture_seed=None if args.input1d else args.seed
base=Path('release/native-vit');job=base/(f'chain31-{args.last_block}-input-{input_sha[:12]}' if input_sha else f'block31-64-{args.seed}' if args.last_block==31 else f'chain31-{args.last_block}-64-{args.seed}');job.mkdir(exist_ok=True)
reports=[];report_path=job/'validation.json';report_path.write_text(json.dumps({'status':'running','seed':fixture_seed,'input_sha256':input_sha,'last_block':args.last_block})+'\n')
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_VIT_')}
def run(argv):subprocess.run(argv,check=True,env=env)
def coordinates(C):
 return bits(64*C,[2,6,7,8,C.bit_length()+3,C.bit_length()+4]),bits(64*C,[0,1,3,4,5]+list(range(9,C.bit_length()+3)))
def decode(path,C=1024,part=None):
 n=64*C;raw=np.fromfile(path,np.uint8);assert not np.any(raw[n:]) and not np.any((raw[:n]&127)==127)
 if part==2:t,c=bits(n,[1,0,4,5,2,15]),bits(n,[6,3,9,7,8,10,11,12,13,14])
 elif part==1:t,c=bits(n,[3,6,7,8,14,15]),bits(n,[0,1,2,4,5,9,10,11,12,13])
 else:t,c=coordinates(C)
 result=np.empty((64,C),np.float32);result[t,c]=e4m3fn(raw[:n]);return result
def compare(stage,predicted,path,C=1024,part=None):
 target=decode(path,C,part);error=np.abs(predicted-target)
 report={'block':block,'stage':stage,'seed':fixture_seed,'values':target.size,'different':int(np.count_nonzero(error)),'max_error':float(error.max())};reports.append(report)
 print(json.dumps(report),flush=True)
 assert np.array_equal(predicted,target),stage+' differs'
if args.input1d:
 source=args.input1d;x=decode(source)
else:
 x=F(np.random.default_rng(args.seed).normal(0,1,(64,1024)).astype(np.float32));t,c=coordinates(1024);source=job/'input.fp8';quantize(x)[t,c].tofile(source)
for block in range(31,args.last_block+1):
 root=job if args.last_block==31 else job/f'block{block}';root.mkdir(exist_ok=True)
 weights={}
 for layer,name in [(0,'expand'),(1,'contract'),(2,'qkv'),(4,'projection')]:
  weights[name]=base/f'block{block}-{name}.weights'
  subprocess.run(['python3','extract_native_weight_record.py','/home/lmxxf/work/tmp-test/nvngx_dlssnr.dll',f'block{block}.layer{layer}.layer',str(weights[name])],check=True,capture_output=True)
 run(['/tmp/native-vit-expand-oracle',str(source),str(root/'expand.fp8'),str(weights['expand']),'64','32'])
 branch=expand(x,unpack_expand(weights['expand']));compare('expand',branch,root/'expand.fp8',4096)
 run(['/tmp/native-vit-contract-oracle',str(root/'expand.fp8'),str(source),str(weights['contract']),str(root/'contract.fp8'),'64','8'])
 feature=residual_projection(branch,x,*unpack_residual(weights['contract'],4096));compare('contract',feature,root/'contract.fp8')
 run(['/tmp/native-vit-qkv-oracle',str(root/'contract.fp8'),str(weights['qkv']),str(root/'qkv'),'8','8','16'])
 vectors=qkv(feature,*unpack_qkv(weights['qkv']))
 for part,a in enumerate(vectors):compare('QKV'[part],a,root/f'qkv-{part}.fp8',part=part)
 run(['/tmp/native-vit-attention-oracle',*[str(root/f'qkv-{i}.fp8') for i in range(3)],str(root/'attention.fp8'),'8','8','32'])
 attended=attention(*vectors);compare('attention',attended,root/'attention.fp8')
 run(['/tmp/native-vit-contract-oracle',str(root/'attention.fp8'),str(root/'contract.fp8'),str(weights['projection']),str(root/'projection.fp8'),'64','8','projection'])
 result=residual_projection(attended,feature,*unpack_residual(weights['projection'],1024));compare('projection',result,root/'projection.fp8')
 x=result;source=root/'projection.fp8'
report_path.write_text(json.dumps({'status':'pass','seed':fixture_seed,'input_sha256':input_sha,'last_block':args.last_block,'stages':reports,'scope':'64-token original CUBIN vs CPU; not AMD or game acceptance'},indent=2)+'\n')
