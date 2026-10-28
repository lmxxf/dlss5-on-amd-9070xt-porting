from pathlib import Path
import subprocess,yaml,json,hashlib,struct,re
R=Path.cwd();O=Path(__file__).parent;P=O.parent/'final-payload/HIP';sums={n:h for h,n in (l.split() for l in (R/'hip/SHA256SUMS').read_text().splitlines() if l.strip())};rows=[]
for f in sorted(P.glob('gfx*/*.hsaco')):
 name=f.relative_to(P).as_posix();sha=hashlib.sha256(f.read_bytes()).hexdigest();assert sha==sums[name],name
 text=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--notes',str(f)],text=True);md=yaml.safe_load(text.split('AMDGPU Metadata: ---',1)[1].split('...',1)[0]);target=md['amdhsa.target'];assert re.search(r'gfx[0-9]+',target).group()==f.parent.name,(name,target)
 rows.append({'path':name,'sha256':sha,'bytes':f.stat().st_size,'target':target,'elf_flags':struct.unpack_from('<I',f.read_bytes(),48)[0]})
assert len(rows)==80 and len(sums)==80
assert (P/'SHA256SUMS').read_bytes()==(R/'hip/SHA256SUMS').read_bytes()
for arch in ['gfx1200','gfx1201']:
 expected={r['path'].split('/')[-1]:r['sha256'] for r in rows if r['path'].startswith(arch+'/')}
 actual={n:h for h,n in (l.split() for l in (P/arch/'SHA256SUMS').read_text().splitlines() if l.strip())};assert actual==expected
(O/'modules-80.json').write_text(json.dumps({'count':80,'action':'reuse unchanged accepted v0.45 modules; knife2/3 STOP','gfx1200_hardware_tested':False,'rows':rows},indent=2));print('80 SHA/ELF targets and all manifests verified')
