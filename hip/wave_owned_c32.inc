// One complete 64-token C32 window per wave; chain FP8 input/output.
#ifndef CW_LAUNDER
#define CW_LAUNDER 0
#endif
#ifndef CW_ROLL_HIDDEN
#define CW_ROLL_HIDDEN 0
#endif
#ifndef CW_QKV_FENCE
#define CW_QKV_FENCE 0
#endif
#ifndef CW_ROLL_WINDOW
#define CW_ROLL_WINDOW 0
#endif
#ifndef CW_PREFIX_SPLIT
#define CW_PREFIX_SPLIT 0 /* 1: prefix features split across both half-waves (bit-exact; 1080 -0.03ms, results/vit-attn-c32-input-20260926) */
#endif
#ifndef CW_TAIL_QUAD
#define CW_TAIL_QUAD 0 /* 1: finish/prefix tail walks 2x2 quads once: 4 LDS reads feed both main and down (was 64+64 reads), 32-bit byte offsets for stores; arithmetic unchanged */
#endif
#ifndef CW_VEC_INPUT
#define CW_VEC_INPUT 0 /* 1: mapped/post input rows read as b128/b64 instead of per-element loads (bit-exact; results/c32-wave-phase-20260926) */
#endif
using cw_i16=int __attribute__((ext_vector_type(16)));
DEV i2 cw_input(const unsigned char*in,uint win,uint qt,uint ci,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py,uint r,uint g){
 uint ww=width+2*sx;int x=int((win%(ww/8))*8+(qt*16+r)%8)-int(sx),y=int((win/(ww/8))*8+(qt*16+r)/8)-int(sy);i2 a{};
 if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint xx=uint(x)+px,yy=uint(y)+py,tile=(yy/8)*(prevw/8)+xx/8,tok=(yy%8)*8+xx%8;__builtin_memcpy(&a,in+(tile*64+tok)*32+ci*16+g*8,8);}return a;
}
template<bool Chain,bool Finish=false,bool DownCrop=false,bool Post=false,bool Prefix=false>
DEV void cw_body(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py,float*main=nullptr,float*down=nullptr,const unsigned char*skip=nullptr,const float*scales=nullptr,const float*color=nullptr,const float*headw=nullptr,float*rgb=nullptr,float rgbscale=0.f,const float*history=nullptr,uint seed=0,uint temporal=0){fp8_sat_mode();
 uint win=bid();if(win>=windows)return;
 __attribute__((shared)) _Float16 residual[4*2*32*8];
 cw_i16 keys{},values{},queries{};
#if CW_ROLL_WINDOW
 _Pragma("clang loop unroll(disable)")
#else
 _Pragma("unroll 4")
#endif
 for(uint qt=0;qt<4;qt++){
  uint lane=__builtin_amdgcn_workitem_id_x();
#if CW_LAUNDER
  __asm__ volatile("" : "+v"(lane));lane&=31u;
#endif
  uint r=lane&15u,g=lane>>4;
  i2 input[2];f8 ffn[2]{};
  if constexpr(Chain){for(uint ci=0;ci<2;ci++)input[ci]=cw_input(in,win,qt,ci,width,height,sx,sy,prevw,px,py,r,g);}
  else if constexpr(Prefix){
#if CW_PREFIX_SPLIT
   // B1 prefix split: fused_prefix_values ran on the g==0 half only (16 lanes idle) and then shipped values 8..15 with eight
   // bpermutes. Here both halves run the same instruction stream on the same token r: the half-wave selects which Box-Muller
   // pair it evaluates (g==1: a,c -> g0; g==0: b,d -> g1,g2), and g0 is the only value exchanged (one bpermute). Each value's
   // operation sequence is identical to fused_prefix_values, so the features, and everything downstream, are bit-identical.
   h8 a{};
   {const float*rgba=reinterpret_cast<const float*>(in);uint p=win*64+qt*16+r,tile=p/64,x=(tile%(width/8))*8+p%8,y=(tile/(width/8))*8+(p%64)/8,rgbp=y*width+x;
    uint h=production_pcg((x*0x8da6b343u)^(y*0xd8163841u)^(seed*0x9e3779b9u)^0x243f6a88u);
    float u1=production_uniform(g?h*0xcaa5b80du+0x21dd796bu:h*0x2c9277b5u+0xac564b05u),u2=production_uniform(g?h*0x83232c31u+0x3463e0acu:h*0xfa6dc5f9u+0x4712a88eu);
    float rr=__builtin_amdgcn_sqrtf(-2.f*(__builtin_amdgcn_logf(u1)*.6931471805599453f)),tt=(6.283185482025146f*u2)*.15915494309189535f;
    float gc=Hrtz(rr*__builtin_amdgcn_cosf(tt)),gs=Hrtz(rr*__builtin_amdgcn_sinf(tt));
    float g0=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute((r+16)*4,__builtin_bit_cast(int,gc)));
    float red=Hrtz(Hrtz(Hrtz(rgba[rgbp*4])-.5f)*.125f),green=Hrtz(Hrtz(Hrtz(rgba[rgbp*4+1])-.5f)*.125f),blue=Hrtz(Hrtz(Hrtz(rgba[rgbp*4+2])-.5f)*.125f);
    float hr=red,hg=green,hb=blue;
    if(temporal){uint q=(y*width+x)*4;hr=Hrtz(Hrtz(Hrtz(history[q])-.5f)*.125f);hg=Hrtz(Hrtz(Hrtz(history[q+1])-.5f)*.125f);hb=Hrtz(Hrtz(Hrtz(history[q+2])-.5f)*.125f);}
    const float lo[8]={gc,gs,hg,hb,g0,1,.0078125f,1},hi[8]={red,green,1,1,blue,hr,1,0};
    for(uint e=0;e<8;e++)a[e]=(_Float16)(g?hi[e]:lo[e]);}
#else
   float pv[16]{};if(g==0)fused_prefix_values(reinterpret_cast<const float*>(in),history,win*64+qt*16+r,width,seed,temporal,pv);
   h8 a{};for(uint e=0;e<8;e++){float hi=__builtin_bit_cast(float,__builtin_amdgcn_ds_bpermute(r*4,__builtin_bit_cast(int,pv[8+e])));a[e]=(_Float16)(g?hi:pv[e]);}
#endif
   for(uint ci=0;ci<2;ci++){
    h8 b{};for(uint e=0;e<8;e++)b[e]=(_Float16)fw[(ci*16+r)*16+g*8+e];
    f8 pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(b,a,f8{});h8 zero{};
    pre=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(zero,zero,pre);
    i2 packed{};for(uint e=0;e<8;e++){float v=Hrtz(pre[e]);put_bits(packed,e,fp8(v));ffn[ci][e]=clampf(v,-448.f,448.f)*fw[8704+ci*16+g*8+e];}input[ci]=packed;
   }
  }
  else{
   uint ww=width+2*sx;int x=int((win%(ww/8))*8+(qt*16+r)%8)-int(sx),y=int((win/(ww/8))*8+(qt*16+r)/8)-int(sy);
   bool valid=x>=0&&y>=0&&x<int(width)&&y<int(height);
#if CW_VEC_INPUT
   // The lane's 8 channels c=ci*16+g*8..+7 are contiguous in every source (f32 features, FP8 skip bytes, scales, FFN residual
   // scale), 32-byte aligned (hipMalloc bases, channel offsets multiple of 8). Read them as whole rows (2x b128 / 1x b64)
   // instead of 8 per-element loads each; the per-element arithmetic and its order are unchanged.
   for(uint ci=0;ci<2;ci++){uint c0=ci*16+g*8;float src[8]{};
    if(valid){
     if constexpr(Post){uint p=uint(y)*width+uint(x),low=(uint(y)/2)*(width/2)+uint(x)/2;
      float lo[8],s0[8],s1[8];unsigned long long sk;
      __builtin_memcpy(lo,__builtin_assume_aligned(reinterpret_cast<const float*>(in)+low*32+c0,32),32);
      __builtin_memcpy(s0,__builtin_assume_aligned(scales+c0,32),32);__builtin_memcpy(s1,__builtin_assume_aligned(scales+32+c0,32),32);
      __builtin_memcpy(&sk,__builtin_assume_aligned(skip+p*32+c0,8),8);
      for(uint e=0;e<8;e++){float high=__builtin_amdgcn_cvt_f32_fp8(int((sk>>(8*e))&255u),0);src[e]=Hrtz(Hrtz(lo[e]*s0[e])+high*s1[e]);}
     }else __builtin_memcpy(src,__builtin_assume_aligned(reinterpret_cast<const float*>(in)+(uint(y)*width+uint(x))*32+c0,32),32);
    }
    float res[8];__builtin_memcpy(res,__builtin_assume_aligned(fw+8704+c0,32),32);
    i2 a{};for(uint e=0;e<8;e++){float v=src[e];put_bits(a,e,fp8(v));ffn[ci][e]=clampf(float((_Float16)v),-448.f,448.f)*res[e];}input[ci]=a;}
#else
   for(uint ci=0;ci<2;ci++){i2 a{};for(uint e=0;e<8;e++){uint c=ci*16+g*8+e;float v=0.f;if(valid){if constexpr(Post){uint p=uint(y)*width+uint(x),low=(uint(y)/2)*(width/2)+uint(x)/2;float high=__builtin_amdgcn_cvt_f32_fp8(int(skip[p*32+c]),0);v=Hrtz(Hrtz(reinterpret_cast<const float*>(in)[low*32+c]*scales[c])+high*scales[32+c]);}else v=reinterpret_cast<const float*>(in)[(uint(y)*width+uint(x))*32+c];}put_bits(a,e,fp8(v));ffn[ci][e]=clampf(float((_Float16)v),-448.f,448.f)*fw[8704+c];}input[ci]=a;}
#endif
  }
  if constexpr(Chain)for(uint part=0;part<3;part++)for(uint ci=0;ci<2;ci++)for(uint kt=0;kt<2;kt++){
   i2 b=matrix8(fw,34944+((part*2+ci)*2+kt)*512+(g*16+r)*8);
   ffn[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,input[kt],ffn[ci]);
  }
#if CW_ROLL_HIDDEN
  _Pragma("clang loop unroll(disable)")
#endif
  for(uint ht=0;ht<8;ht++){
   f8 expanded{};for(uint kt=0;kt<2;kt++){i2 b=matrix8(fw+512,(ht*16+r)*32+kt*16+g*8);expanded=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,input[kt],expanded);}
   i2 a{};for(uint e=0;e<8;e++){float v=expanded[e],g=clampf(v,-4.f,4.f),q=absf(g)*(-.055908203125f)+.447265625f,p=g*q+.89453125f;put_bits(a,e,fp8(v*p));}
   for(uint ci=0;ci<2;ci++){i2 b=matrix8(fw+4608,(ci*16+r)*128+ht*16+g*8);ffn[ci]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,a,ffn[ci]);}
  }
  i2 feature[2];for(uint ci=0;ci<2;ci++){i2 a{};for(uint e=0;e<8;e++){float v=Hrtz(ffn[ci][e]);residual[((qt*2+ci)*32+(g*16+r))*8+e]=(_Float16)v;put_bits(a,e,fp8(v));}feature[ci]=a;}
  for(uint part=0;part<3;part++){
   uint qlane=__builtin_amdgcn_workitem_id_x();
#if CW_QKV_FENCE
   __asm__ volatile("" : "+v"(qlane));qlane&=31u;
#endif
   uint qr=qlane&15u,qg=qlane>>4;
   f8 q[2]{};for(uint ci=0;ci<2;ci++)for(uint kt=0;kt<2;kt++){i2 b=matrix8(w,part*1024+(ci*16+qr)*32+kt*16+qg*8);q[ci]=part<2?__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,feature[kt],q[ci]):__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(feature[kt],b,q[ci]);}
   f8 sum{};if(part<2)for(uint ci=0;ci<2;ci++){h8 squares{},ones{};for(uint e=0;e<8;e++){squares[e]=(_Float16)(q[ci][e]*q[ci][e]);ones[e]=(_Float16)1.f;}sum=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,squares,sum);}
   for(uint ci=0;ci<2;ci++){i2 a{};for(uint e=0;e<8;e++){float v=q[ci][e];if(part<2)v*=__builtin_amdgcn_rsqf(maxf(sum[e],6.198883056640625e-5f))*(part==0?w[8192]:1.f);put_bits(a,e,fp8(v));}
    if(part==0){queries[qt*4+ci*2]=a[0];queries[qt*4+ci*2+1]=a[1];}else if(part==1){keys[qt*4+ci*2]=a[0];keys[qt*4+ci*2+1]=a[1];}else{values[qt*4+ci*2]=a[0];values[qt*4+ci*2+1]=a[1];}
   }
#if CW_QKV_FENCE
   __builtin_amdgcn_sched_barrier(0);
#endif
  }
  __builtin_amdgcn_sched_barrier(0);
 }
 _Pragma("clang loop unroll(disable)") for(uint qt=0;qt<4;qt++){
  uint lane=__builtin_amdgcn_workitem_id_x();
#if CW_LAUNDER
  __asm__ volatile("" : "+v"(lane));lane&=31u;
#endif
  uint r=lane&15u,g=lane>>4;
  i2 q[2];for(uint ci=0;ci<2;ci++)q[ci]=i2{queries[qt*4+ci*2],queries[qt*4+ci*2+1]};
  h8 ex[4];
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++){
   f8 acc{};for(uint ci=0;ci<2;ci++)acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12((i2{keys[kt*4+ci*2],keys[kt*4+ci*2+1]}),q[ci],acc);
   h8 a{};for(uint e=0;e<8;e++){uint row=qt*16+r,k=kt*16+g*8+e;float score=acc[e]+w[4096+row*64+k],affine=clampf(score*.044921875f+1.30078125f,1.03125f,1.5693359375f);uint ah=(bits(affine)>>13)-0x1c000u;ushort hb=static_cast<ushort>(((ah<<5)+0x8000u)&65535u);a[e]=__builtin_bit_cast(_Float16,hb);}ex[kt]=a;
  }
  f8 sums{};h8 ones{};for(uint e=0;e<8;e++)ones[e]=(_Float16)1.f;
  _Pragma("unroll 4") for(uint kt=0;kt<4;kt++)sums=__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12(ones,ex[kt],sums);
  i2 prob[4];_Pragma("unroll 4") for(uint kt=0;kt<4;kt++){i2 a{};for(uint e=0;e<8;e++)put_bits(a,e,fp8(float(ex[kt][e])*norm_inverse(sums[e])));prob[kt]=a;}
  i2 av[2];for(uint ci=0;ci<2;ci++){f8 acc{};_Pragma("unroll 4") for(uint kt=0;kt<4;kt++)acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12((i2{values[kt*4+ci*2],values[kt*4+ci*2+1]}),prob[kt],acc);i2 a{};for(uint e=0;e<8;e++)put_bits(a,e,fp8(acc[e]));av[ci]=a;}
  for(uint ci=0;ci<2;ci++){
   f8 acc{};for(uint kt=0;kt<2;kt++){i2 b=matrix8(w,3072+(ci*16+r)*32+kt*16+g*8);acc=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(b,av[kt],acc);}
   i2 a{};for(uint e=0;e<8;e++){float res=float(residual[((qt*2+ci)*32+(g*16+r))*8+e]),prod=res*w[8193+ci*16+g*8+e],v=Hrtz(acc[e]+prod);if constexpr(Finish||Post)residual[((qt*2+ci)*32+(g*16+r))*8+e]=(_Float16)v;else put_bits(a,e,fp8(F(float((_Float16)v))));}
   if constexpr(!Finish&&!Post)__builtin_memcpy(out+(win*64+qt*16+r)*32+ci*16+g*8,&a,8);
  }
  __builtin_amdgcn_sched_barrier(0);
 }

 if constexpr(Post){
  WG_FENCE(3);WG_FENCE(2);
  uint lane=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  for(uint t=lane;t<64;t+=32){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){
   uint p=uint(y)*width+uint(x);float sums[3]{};
   for(uint c=0;c<32;c++){float f=float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);sums[0]+=f*headw[c];sums[1]+=f*headw[32+c];sums[2]+=f*headw[64+c];}
   for(uint c=0;c<3;c++){float acc=Hrtz(sums[c]),centered=color[p*4+c]*.125f-.0625f,v=(acc*rgbscale+centered)*8.f+.5f;rgb[p*3+c]=clampf(v,0.f,1.f);}
  }}
 }
 if constexpr(Finish){
  WG_FENCE(3);WG_FENCE(2);
  uint c=__builtin_amdgcn_workitem_id_x(),ww=width+2*sx,tx=(win%(ww/8))*8,ty=(win/(ww/8))*8;
  auto load=[&](uint t)->float{return float(residual[((t/16*2+c/16)*32+(c%16/8*16+t%16))*8+c%8]);};
#if CW_TAIL_QUAD
  auto put_main=[&](uint t,float raw_v){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint dest=(uint(y)*width+uint(x))*32+c;float v=F(raw_v);
   if constexpr(Prefix)*reinterpret_cast<unsigned char*>(reinterpret_cast<char*>(main)+dest)=static_cast<unsigned char>(fp8(v));else *reinterpret_cast<float*>(reinterpret_cast<char*>(main)+dest*4u)=v;}};
  for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float a0=load(t),a1=load(t+1),b0=load(t+8),b1=load(t+9);
   if(main){put_main(t,a0);put_main(t+1,a1);put_main(t+8,b0);put_main(t+9,b1);}
   if(down){float top=Hrtz(a0+a1),bottom=Hrtz(b0+b1);uint x=tx+t%8,y=ty+t/8;
    if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))*reinterpret_cast<float*>(reinterpret_cast<char*>(down)+((uint(cy)*(width/2)+uint(cx))*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
    else *reinterpret_cast<float*>(reinterpret_cast<char*>(down)+(((y/2)*(ww/2)+x/2)*32+c)*4u)=F(Hrtz(Hrtz(top+bottom)*.25f));}
  }
  if(false)
#endif
  if(main)for(uint t=0;t<64;t++){int x=int(tx+t%8)-int(sx),y=int(ty+t/8)-int(sy);if(x>=0&&y>=0&&x<int(width)&&y<int(height)){uint dest=(uint(y)*width+uint(x))*32+c;float v=F(load(t));if constexpr(Prefix)reinterpret_cast<unsigned char*>(main)[dest]=static_cast<unsigned char>(fp8(v));else main[dest]=v;}}
  if(!CW_TAIL_QUAD&&down)for(uint k=0;k<16;k++){uint t=(k/4)*16+(k%4)*2;float top=Hrtz(load(t)+load(t+1)),bottom=Hrtz(load(t+8)+load(t+9));uint x=tx+t%8,y=ty+t/8;
   if constexpr(DownCrop){int cx=int(x/2)-int(sx/2),cy=int(y/2)-int(sy/2);if(cx>=0&&cy>=0&&cx<int(width/2)&&cy<int(height/2))down[(uint(cy)*(width/2)+uint(cx))*32+c]=F(Hrtz(Hrtz(top+bottom)*.25f));}
   else down[((y/2)*(ww/2)+x/2)*32+c]=F(Hrtz(Hrtz(top+bottom)*.25f));
  }
 }
}
#define CW_ENTRY KERNEL __attribute__((amdgpu_flat_work_group_size(32,32)))
CW_ENTRY void c32_wave1_chain(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true>(in,fw,w,out,windows,mode,raw,width,height,sx,sy,prevw,px,py);}
CW_ENTRY void c32_wave1_mapped(const unsigned char*in,const float*fw,const float*w,unsigned char*out,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy){cw_body<false>(in,fw,w,out,windows,mode,raw,width,height,sx,sy,0,0,0);}
#define CW_FINISH(NAME,DC) CW_ENTRY void NAME(const unsigned char*in,const float*fw,const float*w,float*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint sx,uint sy,uint prevw,uint px,uint py){cw_body<true,true,DC>(in,fw,w,nullptr,windows,mode,raw,width,height,sx,sy,prevw,px,py,main,down);}
CW_FINISH(c32_wave1_finish,false)
CW_FINISH(c32_wave1_finish_dcrop,true)
CW_ENTRY void c32_wave1_post(const unsigned char*low,const unsigned char*skip,const float*scales,const float*fw,const float*w,const float*color,const float*headw,float*rgb,uint windows,uint width,uint height,uint sx,uint sy,float scale){cw_body<false,false,false,true>(low,fw,w,nullptr,windows,0,1,width,height,sx,sy,0,0,0,nullptr,nullptr,skip,scales,color,headw,rgb,scale);}
CW_ENTRY void c32_wave1_prefix(const unsigned char*rgba,const float*history,const float*fw,const float*w,float*main,float*down,uint windows,uint mode,uint raw,uint width,uint height,uint seed,uint temporal){cw_body<false,true,false,false,true>(rgba,fw,w,nullptr,windows,mode,raw,width,height,0,0,0,0,0,main,down,nullptr,nullptr,nullptr,nullptr,nullptr,0.f,history,seed,temporal);}
