#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
typedef unsigned uint;typedef unsigned char u8;
typedef int i2 __attribute__((ext_vector_type(2)));typedef int i8 __attribute__((ext_vector_type(8)));typedef float f8 __attribute__((ext_vector_type(8)));
#define DEV __attribute__((device)) __attribute__((always_inline))
#define KERNEL extern "C" __attribute__((global))
#define WAVE KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
DEV uint bits(float x){union{float f;uint u;}v;v.f=x;return v.u;}
DEV float val(uint x){union{float f;uint u;}v;v.u=x;return v.f;}
DEV float mx(float a,float b){return a>b?a:b;}
DEV float ab(float x){return val(bits(x)&0x7fffffffu);}
DEV float sh(float x,uint lane){return val(__builtin_amdgcn_ds_bpermute(lane*4,bits(x)));}
DEV void had64(float&a,float&b,uint lane){for(uint s=1;s<32;s*=2){float aa=sh(a,lane^s),bb=sh(b,lane^s);a=(lane&s)?aa-a:a+aa;b=(lane&s)?bb-b:b+bb;}float aa=a;a=(a+b)*.125f;b=(aa-b)*.125f;}
DEV int qi(float x,float inv){int i=int(__builtin_rintf(x*inv));return i<-7?-7:i>7?7:i;}
DEV void store_pair(u8*out,uint base,uint lane,int a,int b){uint aa=__builtin_amdgcn_ds_bpermute((lane^1)*4,uint(a)),bb=__builtin_amdgcn_ds_bpermute((lane^1)*4,uint(b));if(!(lane&1)){out[base+lane/2]=u8((a&15)|((aa&15)<<4));out[base+16+lane/2]=u8((b&15)|((bb&15)<<4));}}
WAVE void q4_group(const float*x,const float*s,u8*out,float*sc,uint M,uint rotate,float clip){
 uint m=__builtin_amdgcn_workgroup_id_x(),g=__builtin_amdgcn_workgroup_id_y(),l=__builtin_amdgcn_workitem_id_x(),k=g*64+l;float a=x[m*1024+k]*s[k],b=x[m*1024+k+32]*s[k+32];if(rotate)had64(a,b,l);
 float maxv=mx(ab(a),ab(b));for(uint d=16;d;d>>=1)maxv=mx(maxv,sh(maxv,l^d));float scale=mx(maxv*clip,1.e-10f)/7.f,inv=__builtin_amdgcn_rcpf(scale);if(!l)sc[g*M+m]=scale;
 store_pair(out,m*512+g*32,l,qi(a,inv),qi(b,inv));
}
KERNEL __attribute__((amdgpu_flat_work_group_size(256,256))) void q4_row(const float*x,const float*s,u8*out,float*sc,uint M,uint rotate,float clip){
 uint m=__builtin_amdgcn_workgroup_id_x(),t=__builtin_amdgcn_workitem_id_x(),l=t%32,w=t/32,k=w*64+l;float a=x[m*1024+k]*s[k],b=x[m*1024+k+32]*s[k+32],c=x[m*1024+k+512]*s[k+512],d=x[m*1024+k+544]*s[k+544];if(rotate){had64(a,b,l);had64(c,d,l);}
 float maxv=mx(mx(ab(a),ab(b)),mx(ab(c),ab(d)));for(uint v=16;v;v>>=1)maxv=mx(maxv,sh(maxv,l^v));__attribute__((shared)) float sm[8];if(!l)sm[w]=maxv;__builtin_amdgcn_s_barrier();for(uint j=0;j<8;j++)maxv=mx(maxv,sm[j]);float scale=mx(maxv*clip,1.e-10f)/7.f,inv=__builtin_amdgcn_rcpf(scale);if(!t)sc[m]=scale;
 store_pair(out,m*512+w*32,l,qi(a,inv),qi(b,inv));store_pair(out,m*512+w*32+256,l,qi(c,inv),qi(d,inv));
}
DEV u8 activation(float v){float g=v<-4?-4:v>4?4:v;float poly=__builtin_fmaf(g,ab(g)*(-.055908203125f)+.447265625f,.89453125f);float z=v*poly;z=z<-448?-448:z>448?448:z;if((bits(z)&0x7fffffffu)==0)z=0;return u8(__builtin_amdgcn_cvt_pk_fp8_f32(z,0.f,0,false));}
template<bool Grouped,uint Tiles=4> DEV void int4_expand_body(const u8*x,const u8*w,const float*xs,const float*ws,u8*out,float*pre,uint M){
 uint block=__builtin_amdgcn_workgroup_id_x(),lane=__builtin_amdgcn_workitem_id_x(),rc=lane%16,gr=lane/16,first=block/(4096/(16*Tiles))*16,col=block%(4096/(16*Tiles))*(16*Tiles);if(first>=M)return;
 i8 acc[Tiles]{};f8 total[Tiles]{};
 for(uint k=0;k<1024;k+=32){i2 a;__builtin_memcpy(&a,x+(first+rc)*512+k/2+gr*8,8);
  for(uint j=0;j<Tiles;j++){i2 b;uint row=col+j*16+rc;__builtin_memcpy(&b,w+((row/16)*16+k/64)*512+(((k%64)/32*2+gr)*16+row%16)*8,8);acc[j]=__builtin_amdgcn_wmma_i32_16x16x32_iu4_w32_gfx12(true,a,true,b,acc[j],false);}
  if constexpr(Grouped){if(k%64==32){uint g=k/64;for(uint j=0;j<Tiles;j++){float sw=ws[g*4096+col+j*16+rc];for(uint e=0;e<8;e++)total[j][e]+=float(acc[j][e])*xs[g*M+first+gr*8+e]*sw;acc[j]=i8{};}}}
 }
 for(uint j=0;j<Tiles;j++)for(uint e=0;e<8;e++){uint row=first+gr*8+e,cc=col+j*16+rc,at=row*4096+cc;float v;if constexpr(Grouped)v=total[j][e];else v=float(acc[j][e])*xs[row]*ws[cc];if(pre)pre[at]=v;out[at]=activation(v);}
}

WAVE void int4_expand_row(const u8*x,const u8*w,const float*xs,const float*ws,u8*out,float*pre,uint M,uint unused){int4_expand_body<false>(x,w,xs,ws,out,pre,M);}
WAVE void int4_expand_group(const u8*x,const u8*w,const float*xs,const float*ws,u8*out,float*pre,uint M,uint unused){int4_expand_body<true>(x,w,xs,ws,out,pre,M);}
// One wave owns a complete K1024 row: retain its32 values per lane, no block barrier.
WAVE void q4_row_wave(const float*x,const float*s,u8*out,float*sc,uint M,uint rotate,float clip){
 uint m=__builtin_amdgcn_workgroup_id_x(),l=__builtin_amdgcn_workitem_id_x();float v[32],maxv=0;
 #pragma unroll
 for(uint g=0;g<16;g++){uint k=g*64+l;float a=x[m*1024+k]*s[k],b=x[m*1024+k+32]*s[k+32];if(rotate)had64(a,b,l);v[2*g]=a;v[2*g+1]=b;maxv=mx(maxv,mx(ab(a),ab(b)));}
 for(uint d=16;d;d>>=1)maxv=mx(maxv,sh(maxv,l^d));float scale=mx(maxv*clip,1.e-10f)/7.f,inv=__builtin_amdgcn_rcpf(scale);if(!l)sc[m]=scale;
 #pragma unroll
 for(uint g=0;g<16;g++)store_pair(out,m*512+g*32,l,qi(v[2*g],inv),qi(v[2*g+1],inv));
}

WAVE void int4_expand_group_n32(const u8*x,const u8*w,const float*xs,const float*ws,u8*out,float*pre,uint M,uint unused){int4_expand_body<true,2>(x,w,xs,ws,out,pre,M);}
