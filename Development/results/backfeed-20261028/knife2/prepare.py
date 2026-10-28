from pathlib import Path
import importlib.util,json,hashlib
R=Path.cwd();O=R/'Development/results/backfeed-20261028/knife2'
p=R/'hip/deep_fast.hip';s=p.read_text();s='#ifndef VIT_BACKFEED_TAIL\n#define VIT_BACKFEED_TAIL 0\n#endif\n'+s
needle=' if constexpr(!Partial)for(uint n=0;n<4;n++)for(uint e=0;e<8;e++){uint i='
pos=s.index(needle)
s=s[:pos]+'''#if (VIT_BACKFEED_TAIL & 1) && VIT_CONTRACT_OCC_LDS >= 1024
 if constexpr(!Partial && W16 && ByteStream && ByteFOut){
  unsigned char*tile=reinterpret_cast<unsigned char*>(const_cast<uint*>(occ_pad));
  for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)tile[(gr()*8+e)*64+n*16+rc()]=vit_contract_F_byte(VIT_FAST_H(total[n][e]));
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  for(uint j=0;j<2;j++){uint offset=(gr()*16+rc())*16+j*512;w16_i4 v;__builtin_memcpy(&v,tile+offset,16);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+(first+offset/64)*1024+col+offset%64,16),&v,16);}
  return;
 }
#endif
'''+s[pos:];p.write_text(s)
p=R/'hip/vit_stream.inc';s=p.read_text();needle='#pragma unroll\n for(uint t=0;t<TM;t++){if(t>=live)continue;';pos=s.index(needle)
s=s[:pos]+'''#if VIT_BACKFEED_TAIL & 2
 if constexpr(W16 && ByteOut && TM==1 && TN==4){
  __attribute__((shared)) unsigned char tile[1024];
  for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)tile[(gr()*8+e)*64+j*16+rc()]=byte_F(VIT_FAST_H(total[0][j][e]));
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  for(uint j=0;j<2;j++){uint offset=(gr()*16+rc())*16+j*512;w16_i4 v;__builtin_memcpy(&v,tile+offset,16);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+(first+offset/64)*OUT+row+offset%64,16),&v,16);}
  return;
 }
#endif
'''+s[pos:]
a=s.index('void vit_stream_qkv_frag_bin_w5f8_vfrag(');pos=s.index('  #pragma unroll\n  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)out[',a);end=s.index('\n',s.index('=byte_F(acc[j][e]);',pos))
s=s[:pos]+'''#if VIT_BACKFEED_TAIL & 4
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  unsigned char*tile=reinterpret_cast<unsigned char*>(raw);
  for(uint j=0;j<2;j++)for(uint e=0;e<8;e++)tile[(gr()*8+e)*32+j*16+rc()]=byte_F(acc[j][e]);
  WG_FENCE(3);__builtin_amdgcn_wave_barrier();WG_FENCE(2);
  uint offset=lane*16;w16_i4 v;__builtin_memcpy(&v,tile+offset,16);__builtin_memcpy(__builtin_assume_aligned(out+(part*tokens+first+offset/32)*1024+row+offset%32,16),&v,16);
#else
'''+s[pos:end]+'\n#endif'+s[end:];p.write_text(s)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
row=next(x for x in m.recipe(R/'hip') if x[0]=='vit-stream-fast');base=row[2]
for name,mask in [('baseline',0),('contract',1),('project',2),('qkv',4)]: (O/(name+'.hip')).write_text(f'#define VIT_BACKFEED_TAIL {mask}\n'+base)
remote=r'D:\DLSSNR-Lab\backfeed-20261028';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe'
files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}];jobs=[]
for name in ['baseline','contract','project','qkv']:
 f=O/(name+'.hip');files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife2/compile-'+name,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\vit-stream-fast.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(O/'compile.json').write_text(json.dumps({'name':'backfeed-tail-compile','files':files,'jobs':jobs},indent=2))
