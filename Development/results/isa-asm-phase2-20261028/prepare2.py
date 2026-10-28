from pathlib import Path
import shutil,importlib.util,subprocess,json,hashlib
R=Path.cwd();P=R/'Development/results/isa-asm-phase2-20261028';W=P/'work';S=W/'knife2-source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);(S/'Development/HIP').mkdir(parents=True,exist_ok=True);shutil.copy(R/'Development/HIP/swin_persistent_types.h',S/'Development/HIP/swin_persistent_types.h')
p=S/'hip/deep_fast.hip';s=p.read_text();s='#ifndef VIT_ORTHO_MASK\n#define VIT_ORTHO_MASK 0\n#endif\n'+s
at=s.index('WAVE void vit_expand_fp8(')
helper='''DEV i2 ortho_pack8(f8 v){
 _Pragma("unroll 8") for(uint e=0;e<8;e++)v[e]=__builtin_amdgcn_fmed3f(v[e]+0.f,-448.f,448.f);
 i2 r;r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[0],v[1],0,false);r[0]=__builtin_amdgcn_cvt_pk_fp8_f32(v[2],v[3],r[0],true);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[4],v[5],0,false);r[1]=__builtin_amdgcn_cvt_pk_fp8_f32(v[6],v[7],r[1],true);return r;
}
DEV f8 ortho_unpack8(i2 v){return f8{__builtin_amdgcn_cvt_f32_fp8(v[0],0),__builtin_amdgcn_cvt_f32_fp8(v[0],1),__builtin_amdgcn_cvt_f32_fp8(v[0],2),__builtin_amdgcn_cvt_f32_fp8(v[0],3),__builtin_amdgcn_cvt_f32_fp8(v[1],0),__builtin_amdgcn_cvt_f32_fp8(v[1],1),__builtin_amdgcn_cvt_f32_fp8(v[1],2),__builtin_amdgcn_cvt_f32_fp8(v[1],3)};}
'''
s=s[:at]+helper+s[at:]
a=s.index('DEV void vit_expand_blocked_body_w16(');b=s.index('{',a);depth=1;i=b+1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i].replace('vit_expand_blocked_body_w16(','vit_expand_ortho_w16(',1).replace('(a0,b0,accum[n])','(b0,a0,accum[n])').replace('(a1,b1,accum[n])','(b1,a1,accum[n])');tail=body.index(' #if VIT_EXPAND_TAIL_VEC');body=body[:tail]+''' _Pragma("unroll") for(uint n=0;n<4;n++){
  f8 v;_Pragma("unroll") for(uint e=0;e<8;e++){float x=accum[n][e],g=clampf(x,-4.f,4.f),poly=__builtin_fmaf(g,__builtin_fmaf(absf(g),-.055908203125f,.447265625f),.89453125f);v[e]=x*poly;}
  i2 packed=ortho_pack8(v);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+(first+rc())*4096+col+n*16+gr()*8,8),&packed,8);
 }
}'''
s=s[:i]+'\n'+body+'\n'+s[i:];old='vit_expand_blocked_body_w16(in,w,out,tokens);';assert s.count(old)==1;s=s.replace(old,'if(VIT_ORTHO_MASK&1)vit_expand_ortho_w16(in,w,out,tokens);else '+old)
contract='''DEV void vit_contract_ortho_w16(const float*in,const float*w,const float*skip,float*out,uint tokens){
#if VIT_CONTRACT_OCC_LDS
 __attribute__((shared)) volatile uint occ_pad[VIT_CONTRACT_OCC_LDS/4];
 if(tokens==0xffffffffu){uint lane=__builtin_amdgcn_workitem_id_x();uint i=(bid()*131u+lane)%(VIT_CONTRACT_OCC_LDS/4);occ_pad[i]=i;__builtin_amdgcn_s_barrier();if(occ_pad[(i+1)%(VIT_CONTRACT_OCC_LDS/4)]==5u)out[0]=0.f;}
#endif
 uint first=bid()/16*16,col=bid()%16*64;if(first>=tokens)return;const uint*ip=reinterpret_cast<const uint*>(in);f8 total[4]{};
 _Pragma("unroll") for(uint n=0;n<4;n++){uint c0=col+n*16+gr()*8;i2 sb;__builtin_memcpy(&sb,__builtin_assume_aligned(reinterpret_cast<const unsigned char*>(skip)+(first+rc())*1024+c0,8),8);f8 sv=ortho_unpack8(sb),wv;__builtin_memcpy(&wv,__builtin_assume_aligned(w+1048576+c0,16),32);_Pragma("unroll") for(uint e=0;e<8;e++)total[n][e]=VIT_FAST_H(sv[e]*wv[e]);}
 _Pragma("clang loop unroll(disable)") for(uint part=0;part<4;part++){f8 acc[4]{};
  _Pragma("unroll 2") for(uint k=part*1024;k<(part+1)*1024;k+=32){uint off=((first+rc())*4096+k+gr()*8)/4;i2 a0={int(ip[off]),int(ip[off+1])},a1={int(ip[off+4]),int(ip[off+5])};
   _Pragma("unroll") for(uint n=0;n<4;n++){uint row=col+n*16+rc();w16_i4 bw=vit_weight_fragment_native_w16(w,row,4096,k);i2 b0={bw[0],bw[1]},b1={bw[2],bw[3]};acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b0,a0,acc[n]);acc[n]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b1,a1,acc[n]);}}
  _Pragma("unroll") for(uint n=0;n<4;n++)for(uint e=0;e<8;e++)total[n][e]+=acc[n][e];
 }
 _Pragma("unroll") for(uint n=0;n<4;n++){f8 v;_Pragma("unroll") for(uint e=0;e<8;e++)v[e]=VIT_FAST_H(total[n][e]);i2 packed=ortho_pack8(v);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+(first+rc())*1024+col+n*16+gr()*8,8),&packed,8);}
}
'''
pos=s.index('// Fragment-weight twin');s=s[:pos]+contract+s[pos:];p.write_text(s)
p=S/'hip/vit_stream.inc';s=p.read_text();needle='WAVE void vit_stream_contract_frag_bstream_bout_compactw_w16';at=s.index(needle)
project='''DEV void vit_project_ortho_w16(const float*in,const float*w,const float*skip,float*out,uint tokens,const uint*reuse_gate){if(reuse_gate&&reuse_gate[0])return;
 uint first=bid()/16*16,row=bid()%16*64;if(first>=tokens)return;f8 total[4]{};const unsigned char*wf=reinterpret_cast<const unsigned char*>(w);
 _Pragma("unroll") for(uint j=0;j<4;j++){uint c0=row+j*16+gr()*8;i2 sb;__builtin_memcpy(&sb,__builtin_assume_aligned(reinterpret_cast<const unsigned char*>(skip)+(first+rc())*1024+c0,8),8);f8 sv=ortho_unpack8(sb),wv;__builtin_memcpy(&wv,__builtin_assume_aligned(w+262144+c0,16),32);_Pragma("unroll") for(uint e=0;e<8;e++)total[j][e]=VIT_FAST_H(sv[e]*wv[e]);}
 _Pragma("clang loop unroll(disable)") for(uint p=0;p<4;p++){f8 acc[4]{};
  _Pragma("unroll 2") for(uint k=p*256;k<(p+1)*256;k+=32){i2 x0,x1;__builtin_memcpy(&x0,reinterpret_cast<const unsigned char*>(in)+(first+rc())*1024+k+gr()*8,8);__builtin_memcpy(&x1,reinterpret_cast<const unsigned char*>(in)+(first+rc())*1024+k+16+gr()*8,8);
   _Pragma("unroll") for(uint j=0;j<4;j++){uint r0=row+j*16;w16_i4 yw;__builtin_memcpy(&yw,wf+((r0/16)*32+k/32)*512+(gr()*16+rc())*16,16);i2 y0={yw[0],yw[1]},y1={yw[2],yw[3]};acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y0,x0,acc[j]);acc[j]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(y1,x1,acc[j]);}}
  _Pragma("unroll") for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)total[j][e]+=acc[j][e];
 }
 _Pragma("unroll") for(uint j=0;j<4;j++){f8 v;_Pragma("unroll") for(uint e=0;e<8;e++)v[e]=VIT_FAST_H(total[j][e]);i2 packed=ortho_pack8(v);__builtin_memcpy(__builtin_assume_aligned(reinterpret_cast<unsigned char*>(out)+(first+rc())*1024+row+j*16+gr()*8,8),&packed,8);}
}
'''
s=s[:at]+project+s[at:]
old='vit_contract_blocked_body<false,false,true,false,true,true,1048576,true>(in,w,skip,out,tokens);';assert s.count(old)==1;s=s.replace(old,'{if(VIT_ORTHO_MASK&2)vit_contract_ortho_w16(in,w,skip,out,tokens);else '+old+'}')
old='vit_stream_project_tiles<1,4,true,false,true,262144,true,true>(in,w,skip,out,tokens,reuse_gate);';assert s.count(old)==1;s=s.replace(old,'if(VIT_ORTHO_MASK&4)vit_project_ortho_w16(in,w,skip,out,tokens,reuse_gate);else '+old);p.write_text(s)
for f in ['hip/deep_fast.hip','hip/vit_stream.inc']:(P/('knife2-'+Path(f).name+'.patch')).write_text(subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout)
spec=importlib.util.spec_from_file_location('recipe',R/'Development/tools/llvm-fork/compile-modules.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);sources={x[0]:x[2] for x in m.recipe(S/'hip')};files=[];jobs=[];remote=r'D:\DLSSNR-Lab\isa-asm-phase2-20261028';rtc=r'D:\DLSSNR-Lab\gap-phase5-20261023\rtc_compile-current.exe';files=[{'path':rtc,'sha256':'553385a0ba6465f332208cfd5830224e255db9973101050f760e3117e5575197'}]
for role,mask,module in [('expand',1,'deep_fast-packed-fast'),('contract',2,'vit-stream-fast'),('project',4,'vit-stream-fast')]:
 f=W/('knife2-'+role+'.hip');f.write_text('#define VIT_ORTHO_MASK '+str(mask)+'\n'+sources[module]);files.append({'path':remote+'\\'+f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()});jobs.append({'label':'knife2/compile-'+role,'exe':rtc,'cpu_compile':True,'env':{'RTC_EXTRA_OPTS':''},'arguments':'"{OUT}\\'+module+'.hsaco" "'+remote+'\\'+f.name+'" comgr gfx1201'})
(P/'knife2-compile.json').write_text(json.dumps({'name':'phase2-vit-orthogonal-compile','files':files,'jobs':jobs},indent=2))
