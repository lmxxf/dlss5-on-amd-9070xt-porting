from pathlib import Path
import re,json
P=Path(__file__).parent;out={}
for role,path in [('baseline',P/'work/debug-map/gfx1201/c64-wave2-fast.hsaco.s'),('ffnb2',P/'work/ffnb2/gfx1201/c64-wave2-fast.hsaco.s')]:
 fn='';r={}
 for l in path.read_text().splitlines():
  m=re.search(r'\.type\s+(\w+),@function',l)
  if m:fn=m[1]
  m=re.search(r'; Occupancy: (\d+)',l)
  if m:r[fn]=int(m[1])
 out[role]={n:r[n] for n in ['c64_wave2_bi_bo','c128_wave2_bi_bo','c256_wave2_bi_bo_w16','c128_wave2_up_lb_sh']}
(P/'occupancy-estimates.json').write_text(json.dumps(out,indent=2))
