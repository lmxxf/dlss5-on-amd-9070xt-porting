from pathlib import Path
import sys,subprocess,shutil
here=Path(__file__).resolve().parent;root=here.parents[3];out=Path('/tmp/vit-image-overlap-src')
subprocess.run([sys.executable,str(here.parent/'vit-adaptive/prepare.py')],check=True)
shutil.copytree('/tmp/vit-adaptive-src',out,dirs_exist_ok=True)
p=out/'Development/HIP/hip_reference_network.h';s=p.read_text()
s=s.replace('Tensor AdaptiveVitGroup(Tensor input,U n){','Tensor AdaptiveVitGroup(Tensor input,U n){\n  JoinImageSchedule();',1)
s=s.replace('if(reset){U zero[8]{};', 'if(reset)image_prepared=false;\n  if(reset){U zero[8]{};',1)
needle='Run("deep","reuse_image_stats",size_t(tiles)*256,adaptive_image,P(adaptive_image_anchor),P(adaptive_state),P(adaptive_image_signature),P(adaptive_image_delta),W,H);';assert s.count(needle)==1
s=s.replace(needle,'if(!image_prepared)'+needle,1)
s=s.replace(' Tensor adaptive_anchor_in,',(here/'schedule.inc').read_text()+'\n Tensor adaptive_anchor_in,',1)
s=s.replace('adaptive_image=P(color);','adaptive_image=P(color);BeginImageSchedule();',1)
s=s.replace('~Network(){api.hipStreamSynchronize(stream);','~Network(){StopImageSchedule();api.hipStreamSynchronize(stream);',1)
p.write_text(s);print(out)
