"""Compare split stages at half-window extents, without assuming padding rules."""
from pathlib import Path
import os,json,subprocess,argparse
import numpy as np
from native_split_reference import ffwd,attention,attention_window
from native_c64_reference import multiply
from native_c32_reference import H,F
from encode_tinlayout_global import quantize
from decode_tinlayout_global import e4m3fn
root=Path('release/native-c512');folder=root/'small-check';folder.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--shift',type=int,choices=(0,1,2,3),default=0);args=parser.parse_args();px=4 if args.shift&1 else 0;py=4 if args.shift&2 else 0
fw=np.load(root/'ffwd-check/matrices.npz');fp=np.load(root/'projection-check/matrices.npz');a=np.load(root/'full-check/attention-matrices.npz')
inverse=np.argsort(np.load(root/'split-view/mapping.npz')['cell_output_to_hwc'])
c=np.arange(512);perm=(c&~3)|((c&1)<<1)|((c&2)>>1)
env={k:v for k,v in os.environ.items() if not k.startswith('DLSS5_SPLIT_')}
for width,height in [(4,4),(8,4),(4,8),(8,12),(12,8)]:
 count=width*height*512;x=F(np.random.default_rng(701).normal(0,.5,(height,width,512)).astype(np.float32))
 quantize(x[...,perm]).reshape(height,width,32,16).transpose(2,0,1,3).copy().tofile(folder/'input.fp8');output=folder/'output.fp8'
 subprocess.run(['/tmp/native-split-global-oracle',str(folder/'input.fp8'),str(output),*[str(root/f'block23-{i}.weights') for i in range(4)],str(width),str(height),str(args.shift),'native-inpview'],env=env,check=True,capture_output=True)
 targets=[]
 for suffix in ('.branch','.ffn','.attn',''):
  raw=np.fromfile(str(output)+suffix,np.uint8);assert not np.any(raw[count:]) and not np.any((raw[:count]&127)==127)
  targets.append(e4m3fn(raw[:count].reshape(-1,8192)[:,inverse]).reshape(height//4,width//4,4,4,512).transpose(0,2,1,3,4).reshape(x.shape))
 branch=ffwd(x,fw);feature=F(multiply(branch,fp['matrix'],H(x*fp['skip'])))
 report={'extent':[width,height],'shift':args.shift,'branch_exact':float(np.mean(branch==targets[0])),'ffn_exact':float(np.mean(feature==targets[1])),'attention_candidates':{}}
 hh,ww=(height+py+7)//8*8,(width+px+7)//8*8
 for mode in ('zero','repeat_y','repeat_x','repeat_xy'):
  padded=np.zeros((hh,ww,512),np.float32)
  for y in range(hh):
   for x0 in range(ww):
    yy=(y-py)%height if mode in ('repeat_y','repeat_xy') else y-py
    xx=(x0-px)%width if mode in ('repeat_x','repeat_xy') else x0-px
    if 0<=yy<height and 0<=xx<width:padded[y,x0]=feature[yy,xx]
  tiles=padded.reshape(hh//8,8,ww//8,8,512).transpose(0,2,1,3,4).reshape(-1,64,512)
  attended=attention(tiles,[a[k] for k in ('Q','K','V')],a['bias'],a['scales']).reshape(hh//8,ww//8,8,8,512).transpose(0,2,1,3,4).reshape(hh,ww,512)[py:py+height,px:px+width]
  final=F(multiply(attended,a['P'],H(feature*a['skip'])))
  report['attention_candidates'][mode]={'attn_exact':float(np.mean(attended==targets[2])),'final_exact':float(np.mean(final==targets[3]))}
 print(json.dumps(report,indent=2),flush=True)
 expected='repeat_xy' if width==4 and height==4 else 'repeat_x' if width==4 else 'repeat_y' if height==4 else 'zero'
 assert report['branch_exact']==1 and report['ffn_exact']==1
 assert all(v==1 for v in report['attention_candidates'][expected].values()), 'small split boundary rule differs'
 assert np.array_equal(attention_window(feature,[a[k] for k in ('Q','K','V')],a['bias'],a['scales'],args.shift),targets[2]), 'reusable split window contract differs'
 np.savez(folder/f'fixture-{width}x{height}-shift{args.shift}.npz',input=x,padding_mode=expected,**{f'oracle_{i}':target for i,target in enumerate(targets)})
print(json.dumps({'shift':args.shift,'validated_extents':5,'status':'all_expected_modes_exact'}))
