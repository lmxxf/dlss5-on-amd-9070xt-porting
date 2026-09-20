// Head-major K and transposed V for exact and pooled paths. One workgroup per head/32-token group.
// Pair adjacent horizontal tokens; if an odd-width row splits a pair, use one vertical pair
// and pair the remaining sorted tokens within their rows. Q and exact K/V order never change.
WAVE void vit_select_prep(const unsigned char*in,unsigned char*pool,uint*route,uint tokens,float threshold){
 uint G=(tokens+31)/32,h=bid()/G,g=bid()%G,first=g*32,c=__builtin_amdgcn_workitem_id_x(),col=h*32+c;
 uint KT=tokens/2,PT=(KT+15)&~15u,cols=tokens==400?25:32;
 if(h>=32)return;
 __attribute__((shared)) unsigned char sk[32*36],sv[32*36],spv[16*36];
 for(uint j=0;j<32&&first+j<tokens;j++){
  uint k=in[(tokens+first+j)*1024+col],v=in[(2*tokens+first+j)*1024+col];
  sk[j*36+c]=k;sv[j*36+c]=v;pool[(h*tokens+first+j)*32+c]=k;
 }
 __builtin_amdgcn_fence(3,"workgroup");__builtin_amdgcn_s_barrier();__builtin_amdgcn_fence(2,"workgroup");
 bool full=first+31<tokens;uint a=32,b=32;
 if(full&&cols==25){uint x=first%cols;
  if((x&1u)==0){a=0;b=25;}
  else if(x>=19){a=25-x;b=a+25;}
 }
 float kd=0,kn=0,vd=0,vn=0,unsafe=0;
 for(uint j=0;j<16&&first+2*j+1<tokens;j++){
  uint u=2*j,v=u+1;
  if(a<32){if(j==15){u=a;v=b;}else{if(u>=a)u++;if(u>=b)u++;if(v>=a)v++;if(v>=b)v++;}}
  float k0=__builtin_amdgcn_cvt_f32_fp8(sk[u*36+c],0),k1=__builtin_amdgcn_cvt_f32_fp8(sk[v*36+c],0);
  float v0=__builtin_amdgcn_cvt_f32_fp8(sv[u*36+c],0),v1=__builtin_amdgcn_cvt_f32_fp8(sv[v*36+c],0);
  kd+=(k0-k1)*(k0-k1);kn+=k0*k0+k1*k1;vd+=(v0-v1)*(v0-v1);vn+=v0*v0+v1*v1;
  float vm=F(.5f*(v0+v1));unsafe+=absf(vm)>224.f?1.f:0.f;
  pool[2*tokens*1024+(h*PT+first/2+j)*32+c]=byte_F(.5f*(k0+k1));spv[j*36+c]=byte_F(vm*2.f);
 }
 kd=select_reduce(kd);kn=select_reduce(kn);vd=select_reduce(vd);vn=select_reduce(vn);unsafe=select_reduce(unsafe);
 float distance=__builtin_fmaxf(kd/__builtin_fmaxf(kn,1.e-12f),vd/__builtin_fmaxf(vn,1.e-12f));
 if(c==0){route[(h*G+g)*2]=full&&unsafe==0.f&&distance<=threshold;route[(h*G+g)*2+1]=bits(distance);}
 __builtin_amdgcn_fence(3,"workgroup");__builtin_amdgcn_s_barrier();__builtin_amdgcn_fence(2,"workgroup");
 for(uint r=0;r<32;r++){
  if(first+c<tokens)pool[tokens*1024+(h*32+r)*tokens+first+c]=sv[c*36+r];
  if(c<16&&first/2+c<KT)pool[(2*tokens+PT)*1024+(h*32+r)*PT+first/2+c]=spv[c*36+r];
 }
}
