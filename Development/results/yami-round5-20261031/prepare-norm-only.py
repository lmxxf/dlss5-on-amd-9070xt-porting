from pathlib import Path
import shutil,json,hashlib,re,copy,subprocess,importlib.util
p=Path(__file__).parent;assert json.loads((p/'final-five-decision.json').read_text())['selected']is None
r=p/'production-norm-only/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);shutil.copyfile(p/'production-combined-norm900/source/hip/wave_owned_c32.inc',r/'hip/wave_owned_c32.inc')
f=r/'hip/build-modules.ps1';s=f.read_text();pat=r"(@\{ name\s*=\s*'c32-wave1-fast-norm900';\s*defines\s*=\s*@\()(.*?)(\);\s*sources)";m=re.search(pat,s);assert m;s=s[:m.start()]+m[1]+m[2]+",'CW_EXACT_DEN_SUM 1'"+m[3]+s[m.end():]
a="    $opts = (@($(if ($RowOpts) { $m.opts }), $ExtraOpts) | Where-Object { $_ }) -join ' '";assert s.count(a)==1;s=s.replace(a,"    # Preserve verified production per-architecture C512 deep compiler options.\n    $moduleBackendOptions = $m.opts\n    if ($target -eq 'gfx1201' -and $m.name -eq 'c512-m32-deep') { $moduleBackendOptions = '' }\n    $opts = (@($(if ($RowOpts) { $moduleBackendOptions }), $ExtraOpts) | Where-Object { $_ }) -join ' '");f.write_text(s)
stock=Path('Development/results/ksearch-round4-20261031/payload/HIP');dest=p/'final-minimal/HIP';shutil.copytree(stock,dest,dirs_exist_ok=True);ledger=[]
sp=importlib.util.spec_from_file_location('rec','Development/tools/llvm-fork/compile-modules.py');rec=importlib.util.module_from_spec(sp);sp.loader.exec_module(rec);rows={x[0]:x for x in rec.recipe(r/'hip')}
sp=importlib.util.spec_from_file_location('elf','Development/tools/llvm-fork/compare-kernels.py');elf=importlib.util.module_from_spec(sp);sp.loader.exec_module(elf)
for arch in ['gfx1201','gfx1200']:
 for mod in ['c32-wave1','c32-wave1-rtz','c32-wave1-fast','c32-wave1-fast-norm900']:
  build=p/'production-rebuild-llvm'if mod in ['c32-wave1','c32-wave1-rtz']else p/'production-combined-norm900/build';manifest=json.loads((build/'manifest.json').read_text());row=next(x for x in manifest['modules']if x['target']==arch and x['module']==mod);assert hashlib.sha256(rows[mod][2].encode()).hexdigest()==row['source_sha256'];shutil.copyfile(build/arch/(mod+'.hsaco'),dest/arch/(mod+'.hsaco'))
 for file in (dest/arch).glob('*.hsaco'):
  ref=p/'c32-dotpar/build/gfx1201'/file.name if arch=='gfx1201'and file.name=='c32-wave1-fast-norm900.hsaco'else stock/arch/file.name;a,am,af=elf.elf(file);b,bm,bf=elf.elf(ref);assert am==bm and af==bf and all(a.get(k)==b.get(k)for k in ['.text','.rodata','.data']);ledger.append(dict(arm='minimal',arch=arch,module=file.stem,source_sha256=hashlib.sha256(rows[file.stem][2].encode()).hexdigest(),sha256=hashlib.sha256(file.read_bytes()).hexdigest(),runtime_reference=str(ref),code_addresses_metadata_rodata_data_identical=True))
(dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+f.relative_to(dest).as_posix()+'\n'for f in sorted(dest.glob('gfx*/*.hsaco'))));(p/'minimal-artifact-ledger.json').write_text(json.dumps(ledger,indent=2)+'\n')
remote=r'D:\DLSSNR-Lab\yami-round5-20261031';base=json.loads((p/'r2d-gold.json').read_text());old=remote+'\\candidate\\modules';new=remote+'\\final-minimal\\modules'
for x in base['files']:
 if old in x['path']:x['path']=x['path'].replace(old,new);x['sha256']=hashlib.sha256((dest/'gfx1201'/x['path'].split('\\')[-1]).read_bytes()).hexdigest()
refs=json.loads((p/'c32-reduction-gold-reference.json').read_text())['hashes'];gold=[]
for j0 in base['jobs']:
 if not j0['label'].endswith('-candidate'):continue
 j=copy.deepcopy(j0);prof=j['label'].split('/')[-1].removesuffix('-candidate');j['label']='minimal-gold/'+prof;j['arguments']=j['arguments'].replace(old,new);j['gold']=refs[prof];j.pop('compare_to',None);gold.append(j)
(p/'minimal-gold.json').write_text(json.dumps(dict(name='minimal-gold',files=base['files'],jobs=gold),indent=2)+'\n');jobs=[]
for rnd in [1,2,3]:
 for slot,arm in enumerate(['baseline','candidate','candidate','baseline']):
  j=copy.deepcopy(next(j for j in base['jobs']if j['label']=='r2d-gold/original960-baseline'))if arm=='baseline'else copy.deepcopy(next(j for j in gold if j['label']=='minimal-gold/original960'));j['label']=f'minimal-long/perf-original960-r{rnd}-{slot}-{arm}';a=re.findall(r'"[^"]*"|\S+',j['arguments']);a[4]='800';a[8]='1';j['arguments']=' '.join(a);j['frame_count']=800;j['edges_only']=1;j['gold']=refs['original960'];j.pop('compare_to',None);jobs.append(j)
(p/'minimal-long.json').write_text(json.dumps(dict(name='minimal-long',files=base['files'],jobs=jobs),indent=2)+'\n');subprocess.run(['python3',str(p/'pin-inputs.py'),'minimal-gold','minimal-long'],check=True)
setup=["$ErrorActionPreference='Stop'","if(Test-Path 'D:\\DLSSNR-Lab\\gpu.lock'){throw 'busy'}",f'New-Item -ItemType Directory -Force "{new}"|Out-Null',f'Copy-Item "D:\\DLSSNR-Lab\\ksearch-round4-20261031\\payload\\HIP\\gfx1201\\*" "{new}"']
for mod in ['c32-wave1','c32-wave1-rtz','c32-wave1-fast','c32-wave1-fast-norm900']:setup.append(f'Copy-Item "{remote}\\minimal-{mod}.hsaco" "{new}\\{mod}.hsaco"')
(p/'minimal-setup.ps1').write_text('\n'.join(setup)+'\n');print('80-object minimal payload,onlynorm900gfx1201runtimechanged')
