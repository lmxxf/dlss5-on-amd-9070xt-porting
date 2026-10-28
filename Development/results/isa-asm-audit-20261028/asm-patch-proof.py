from pathlib import Path
import re,subprocess,json,hashlib
P=Path(__file__).parent;W=P/'work/roundtrip';s=(W/'gfx1201/c32-wave1-fast.hsaco.s').read_text();a=s.index('c32_wave1_prefix_b8d_w16:');b=s.index('.Lfunc_end',a);body=s[a:b];m=re.search(r'v_lshlrev_b32_e32 (v\d+), 1, (v\d+)',body);assert m
old=m[0];new=f'v_add_nc_u32_e32 {m[1]}, {m[2]}, {m[2]}';body=body[:m.start()]+new+body[m.end():];patched=s[:a]+body+s[b:];(W/'patched.s').write_text(patched)
cmds=[['/home/lmxxf/work/llvm-build-23/bin/clang','-cc1as','-triple','amdgcn-amd-amdhsa','-target-cpu','gfx1201','-target-feature','-real-true16','-filetype','obj',str(W/'patched.s'),'-o',str(W/'patched.o')],['/home/lmxxf/work/llvm-build-23/bin/ld.lld','--no-undefined','-shared','-plugin-opt=mcpu=gfx1201',str(W/'patched.o'),'-o',str(W/'patched.hsaco')]]
for cmd in cmds:subprocess.run(cmd,check=True)
(P/'asm-patch-proof.json').write_text(json.dumps({'kernel':'c32_wave1_prefix_b8d_w16','old':old,'new':new,'proof':'uint32(x << 1) == uint32(x+x), both opcodes have no VCC/SCC result; same 4-byte instruction size','purpose':'real assembly edit/reassembly/loader proof only, not claimed speedup','sha256':hashlib.sha256((W/'patched.hsaco').read_bytes()).hexdigest(),'commands':cmds},indent=2))
