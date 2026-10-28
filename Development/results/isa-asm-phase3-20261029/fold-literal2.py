from pathlib import Path
import re,subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';S=P/'work/knife2-source';p=S/'hip/wave_owned_mh.inc';s=p.read_text();a=s.index('    _Pragma("unroll") for(uint tile=0;tile<2;tile++){');b=s.index('{',a);i=b+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[b+1:i-1].replace('w2_fold_load<C>','w2_load<C>');replacement=''.join('    {\n'+re.sub(r'\btile\b',str(t)+'u',body)+'\n    }\n' for t in [0,1]);s=s[:a]+replacement+s[i:];p.write_text(s);(P/'knife2-literal.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/wave_owned_mh.inc','--label','b/hip/wave_owned_mh.inc',str(R/'hip/wave_owned_mh.inc'),str(p)],capture_output=True,text=True).stdout)
