from pathlib import Path
import shutil,importlib.util,json,hashlib,subprocess
R=Path.cwd();P=R/'Development/results/isa-asm-phase2-20261028';W=P/'work';S=W/'knife1-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/c512_qkv_attention_compact.inc';s=p.read_text();s='#ifndef C512_WMMA_VFRAG\n#define C512_WMMA_VFRAG 1\n#endif\n'+s
pairs=[('b[cur][j],a[cur][t],accq[t][j]','a[cur][t],b[cur][j],accq[t][j]','j>=4'),('cb[j],ca[t],accq[t][j]','ca[t],cb[j],accq[t][j]','j>=4'),('b,a[t],accq[t][j]','a[t],b,accq[t][j]','j>=4'),('b,a[t],acc[t][ci]','a[t],b,acc[t][ci]','part==2')]
for old,new,cond in pairs:
 op='__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12';s=s.replace(op+'('+old+')','((C512_WMMA_VFRAG&&W16&&!DebugNorm&&('+cond+'))?'+op+'('+new+'):'+op+'('+old+'))')
old='    uint tok=(wave*QT+t)*16+row;\n#if C512_COMPACT_VT';assert old in s;s=s.replace(old,'''    uint tok=(wave*QT+t)*16+row;
    if(C512_WMMA_VFRAG&&W16&&!DebugNorm&&part==2){__builtin_memcpy(packed+128*32+((wave*QT+t)*2+ci)*256+(half*16+row)*8,&a,8);continue;}
#if C512_COMPACT_VT''')
old='''#if C512_COMPACT_VT
    i2 v;__builtin_memcpy(&v,packed+128*32+(ci*16+row)*64+kt*16+half*8,8);''';assert old in s;s=s.replace(old,'''    i2 v;
    if(C512_WMMA_VFRAG&&W16&&!DebugNorm){__builtin_memcpy(&v,packed+128*32+(kt*2+ci)*256+(half*16+row)*8,8);}else{
#if C512_COMPACT_VT
    __builtin_memcpy(&v,packed+128*32+(ci*16+row)*64+kt*16+half*8,8);''');s=s.replace('    i2 v{};_Pragma("unroll 8")','    v=i2{};_Pragma("unroll 8")');s=s.replace('    av=__builtin_amdgcn_wmma','    }\n    av=__builtin_amdgcn_wmma',1);p.write_text(s)
(P/'knife1.patch').write_text(subprocess.run(['diff','-u','--label','a/hip/c512_qkv_attention_compact.inc','--label','b/hip/c512_qkv_attention_compact.inc',str(R/'hip/c512_qkv_attention_compact.inc'),str(p)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for role,hip in [('baseline',R/'hip'),('candidate',S/'hip')]:
 row=next(x for x in m.recipe(hip)if x[0]=='c512-m32-mh');(W/(role+'.hip')).write_text(row[2])
remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for role in ['baseline','candidate']:
 f=W/(role+'.hip');files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife1/compile-'+role,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\c512-m32-mh.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife1-compile.json').write_text(json.dumps({'name':'phase2-v-transpose-compile','files':files,'jobs':jobs},indent=2))
for name in ['run-jobs.ps1','collect.ps1']:(P/name).write_text((R/'Development/results/backfeed-20261028'/name).read_text().replace('backfeed-20261028','isa-asm-phase2-20261028'))
(P/'.gitignore').write_text('/work/\n/knife1/evidence/\n/knife2/evidence/\n/knife3/evidence/\n')
