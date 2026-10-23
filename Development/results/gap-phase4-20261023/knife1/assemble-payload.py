import pathlib,json,hashlib,shutil,struct,re
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');O=pathlib.Path(__file__).parent;W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1');OLD=pathlib.Path('/home/lmxxf/work/dlss5-five-build/final-payload/HIP');D=W/'final-payload/HIP'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=R/'hip/SHA256SUMS';before=manifest.read_text();rows=[]
for line in before.splitlines():
 h,k=line.split();rows.append((k,h))
assert len(rows)==80 and len({k for k,h in rows})==80
new=json.loads((O/'final-modules4.json').read_text());assert len(new)==4
replace={x['target']+'/'+x['name']:x for x in new};assert len(replace)==4
for k,x in replace.items():
 p=pathlib.Path(x['path']);assert sha(p)==x['sha256'] and p.stat().st_size==x['bytes'] and x['actual_elf_target_checked']
 old=dict(rows)[k];assert old!=x['sha256'],('already changed or unexpected old SHA',k)
# Validate all archived old binaries before selecting the four replacements.
for k,h in rows:assert sha(OLD/k)==h,('old payload diverged',k)
checks=[];changed=[]
for k,h in rows:
 src=pathlib.Path(replace[k]['path'])if k in replace else OLD/k
 expect=replace[k]['sha256']if k in replace else h
 dst=D/k;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);actual=sha(dst);assert actual==expect
 data=dst.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==224
 flags=struct.unpack_from('<I',data,48)[0];target=k.split('/')[0];assert flags&255=={'gfx1200':0x48,'gfx1201':0x4e}[target],(k,hex(flags))
 checks.append(dict(path=k,sha256=actual,bytes=len(data),elf_flags=hex(flags),actual_target=target,replaced=k in replace))
 if k in replace:changed.append(dict(path=k,before_sha256=h,after_sha256=actual))
assert sum(x['replaced']for x in checks)==4
expected={x['path']:x['sha256']for x in checks}
after=''.join(expected[line.split()[1]]+line[64:] for line in before.splitlines(keepends=True))
assert sum(a!=b for a,b in zip(before.splitlines(),after.splitlines()))==4
for target in ['gfx1200','gfx1201']:
 selected=[x for x in checks if x['actual_target']==target];assert len(selected)==40
 (D/target/'SHA256SUMS').write_text(''.join(x['sha256']+'  '+x['path'].split('/',1)[1]+'\n' for x in selected))
(D/'SHA256SUMS').write_text(after)
# Persistent receipt precedes the final production-manifest replacement.
(O/'sha256sums-before-vfrag.txt').write_text(before)
receipt={'payload':str(D),'count':80,'per_arch':{'gfx1200':40,'gfx1201':40},'changed':changed,'unchanged_count':76,'all_80_sha_verified':True,'all_80_elf_targets_verified':True,'gfx1200_hardware_tested':False,'production_manifest_before_sha256':hashlib.sha256(before.encode()).hexdigest(),'production_manifest_after_sha256':hashlib.sha256(after.encode()).hexdigest(),'modules':checks}
(O/'final-payload-identity.json').write_text(json.dumps(receipt,indent=2));(D/'modules-identity.json').write_text(json.dumps(checks,indent=2));manifest.write_text(after)
assert manifest.read_bytes()==(D/'SHA256SUMS').read_bytes()
print(json.dumps({k:v for k,v in receipt.items()if k!='modules'},indent=2))
