from pathlib import Path
import zipfile,shutil,json,hashlib,struct,importlib.util,subprocess
p=Path(__file__).parent;out=p/'payload/HIP';out.mkdir(exist_ok=True)
with zipfile.ZipFile(p/'full-binaries.zip')as z:
 for f in z.infolist():
  if not f.is_dir():d=out/f.filename.replace('\\','/');d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(z.read(f))
for arch in ['gfx1200','gfx1201']:
 for f in(p/'work/full-llvm'/arch).glob('*.hsaco'):shutil.copyfile(f,out/arch/f.name)
sp=importlib.util.spec_from_file_location('compare','Development/tools/llvm-fork/compare-kernels.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);stock=p.parent/'isa-asm-phase3-20261029/payload/HIP';mods=[];changed=[]
for f in sorted(out.glob('*/*.hsaco')):
 n=f.relative_to(out).as_posix();arch=f.parent.name;b=f.read_bytes();flags=struct.unpack_from('<I',b,48)[0];headers=subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/llvm-readobj','--file-headers',str(f)],text=True);assert 'EF_AMDGPU_MACH_AMDGCN_'+arch.upper() in headers,(n,headers);_,a,sa=m.elf(stock/n);_,c,sc=m.elf(f);assert set(a)==set(c),(n,'exports')
 diffs=[]
 for k in a:
  code=hashlib.sha256(sa[k][2]).hexdigest()!=hashlib.sha256(sc[k][2]).hexdigest();md=a[k]!=c[k]
  if code or md:diffs.append({'kernel':k,'code_changed':code,'metadata_changed':md,'baseline_vgpr':a[k]['.vgpr_count'],'candidate_vgpr':c[k]['.vgpr_count'],'candidate_private':c[k]['.private_segment_fixed_size'],'candidate_spill':c[k].get('.vgpr_spill_count',0)})
 if diffs:changed.append({'module':n,'changed':diffs})
 mods.append({'path':n,'sha256':hashlib.sha256(b).hexdigest(),'elf_flags':flags,'kernels':len(c),'changed_functions':len(diffs)})
assert len(mods)==80
# Expected production changes are C32 fast/norm900 wave-owned functions only.
for row in changed:
 assert row['module'].split('/')[1]in['c32-wave1-fast.hsaco','c32-wave1-fast-norm900.hsaco'],row
 for x in row['changed']:
  assert x['kernel'].startswith('c32_wave1_'),x
  assert x['candidate_private']==0 and x['candidate_spill']==0,x
  if x['kernel'].endswith('_w16'):assert x['candidate_vgpr']<=160,x
accepted4=False
(p/'full-code-comparison.json').write_text(json.dumps(changed,indent=2));(p/'full-rebuild-verification.json').write_text(json.dumps({'count':80,'all_architectures_verified':True,'accepted_knife4':accepted4,'modules':mods},indent=2));s=''.join(x['sha256']+'  '+x['path']+'\n'for x in mods);Path('hip/SHA256SUMS').write_text(s);(out/'SHA256SUMS').write_text(s);(p/'full-binaries.zip').unlink();print('80 ELF architectures and full function/metadata comparison verified')
