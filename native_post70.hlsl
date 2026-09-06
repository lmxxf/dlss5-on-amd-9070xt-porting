StructuredBuffer<float> input:register(t0),weights:register(t1),skip_or_color:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;float input_scale;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float aligned_half(int sum,float acc,float scale,int e){
 float scaled_acc=acc*scale;
 if(scaled_acc!=trunc(scaled_acc)||abs(scaled_acc)>=67108864.0)return asfloat(0x7fc00000u);
 bool negative=sum<0,aneg=acc<0;uint magnitude=negative?(0u-asuint(sum)):asuint(sum),other=(uint)abs(scaled_acc);
 if(negative==aneg)magnitude+=other;
 else if(magnitude>=other)magnitude-=other;
 else{magnitude=other-magnitude;negative=aneg;}
 if(magnitude==0)return 0;
 int quantum_exp=e-27,top=firstbithigh(magnitude),step_exp=max(top+quantum_exp,-14)-10,drop=step_exp-quantum_exp;
 uint rounded=magnitude;
 if(drop>32)rounded=0;
 else if(drop==32)rounded=magnitude>0x80000000u?1:0;
 else if(drop>0){rounded=magnitude>>drop;uint remainder=magnitude&((1u<<drop)-1u),half=1u<<(drop-1);if(remainder>half||(remainder==half&&(rounded&1u)))rounded++;}
 else step_exp=quantum_exp;
 float value=float(rounded)*exp2(float(step_exp));if(value>=65520.0)value=asfloat(0x7f800000u);
 return negative?-value:value;
}
[numthreads(64,1,1)]void merge(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/32,c=i%32;if(p>=width*height)return;
 uint low=((p/width)/2)*(width/2)+(p%width)/2;
 output[i]=H(H(input[low*32+c]*weights[c])+skip_or_color[i]*weights[32+c]);
}
[numthreads(64,1,1)]void finish(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/3,row=i%3;if(p>=width*height)return;float acc=0;
#if POST_BASE_ONLY == 1
 output[i]=skip_or_color[p*4+row];return;
#endif
#if POST_BASE_ONLY != 2
 [unroll]for(uint part=0;part<2;part++){
  float products[16];int e=acc==0?-1000:int((asuint(acc)>>23)&255u)-125;
  [unroll]for(uint j=0;j<16;j++){float a=input[p*32+part*16+j],b=weights[row*32+part*16+j];products[j]=a*b;if(products[j]!=0)e=max(e,int((asuint(a)>>23)&255u)+int((asuint(b)>>23)&255u)-252);}
  if(e!=-1000){float scale=asfloat(uint(27-e+127)<<23);int sum=0;
   [unroll]for(uint j=0;j<16;j++)sum+=(int)(products[j]*scale);
   acc=aligned_half(sum,acc,scale,e);
  }
 }
#endif
 // Explicit float32 boundaries, matching the original encode/add/decode order.
 precise float base=skip_or_color[p*4+row]*0.125-0.0625;
 precise float encoded=acc*input_scale+base;
 precise float rgb=encoded*8.0+0.5;
 output[i]=clamp(rgb,0.0,1.0);
}
