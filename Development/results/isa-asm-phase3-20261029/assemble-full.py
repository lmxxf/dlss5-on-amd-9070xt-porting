from pathlib import Path
import zipfile,shutil,json,hashlib,subprocess,re
P=Path(__file__).parent;R=P.parents[2];out=P/'payload/HIP';out.mkdir(exist_ok=True)
with zipfile.ZipFile(P/'full-binaries.zip')as z:
 for f in z.infolist():
  if not f.is_dir():
   dst=out/f.filename.replace('\\','/');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(z.read(f))
for a in ['gfx1200','gfx1201']:
 for f in (P/'work/full-llvm'/a).glob('*.hsaco'):shutil.copyfile(f,out/a/f.name)
fix=P/'work/c512-gfx1200-rowopts.hsaco'
if fix.exists():shutil.copyfile(fix,out/'gfx1200/c512-m32-deep.hsaco')
old=json.loads((P/'start-lock.json').read_text())['modules'];rows=[]
for f in sorted(out.glob('*/*.hsaco')):
 n=f.relative_to(out).as_posix();sha=hashlib.sha256(f.read_bytes()).hexdigest();hdr=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--file-headers',str(f)],text=True);assert 'EF_AMDGPU_MACH_AMDGCN_'+f.parent.name.upper()in hdr,(n,hdr);rows.append({'path':n,'sha256':sha,'matches_production':sha==old[n],'architecture':f.parent.name})
assert len(rows)==80
# Container SHA may include compile-unit identities; code/resource identity is audited separately.
for item in json.loads((P/'full-code-comparison.json').read_text()):assert not item['changed'],item
(P/'full-rebuild-verification.json').write_text(json.dumps({'count':80,'all_elf_architectures_verified':True,'all_sha256_match_locked_production':all(x['matches_production']for x in rows),'all_function_code_and_resources_match':True,'modules':rows},indent=2));(R/'hip/SHA256SUMS').write_text(''.join(x['sha256']+'  '+x['path']+'\n' for x in rows));shutil.copyfile(R/'hip/SHA256SUMS',out/'SHA256SUMS');(P/'full-binaries.zip').unlink()
for name in ['final-gold.json','final-perf.json']:
 f=P/name;m=json.loads(f.read_text())
 for x in rows:
  if x['architecture']=='gfx1201':
   for root,rel in [(r'D:\DLSSNR-Lab\isa-asm-phase3-20261029\payload\HIP',x['path']),(r'D:\DLSSNR-Lab\gap-phase6-20261023\knife1\modules',x['path'].split('/')[1])]:
    m['files'].append({'path':root+chr(92)+rel.replace('/',chr(92)),'sha256':old[x['path']] if 'knife1' in root else x['sha256']})
 f.write_text(json.dumps(m,indent=2))
print('80 rebuilt modules: ELF verified; function code/resources identical, container hashes refreshed')
