StructuredBuffer<float> input:register(t0),weights:register(t1),skip_or_color:register(t2);
RWStructuredBuffer<float> output:register(u0);
cbuffer Geometry:register(b0){uint width;uint height;float input_scale;}
float H(float v){uint b=asuint(v),sg=b&0x80000000u,a=b&0x7fffffffu;if(a>=0x7f800000u)return v;if(a<0x38800000u){float q=round(abs(v)*16777216.0)*5.9604644775390625e-8;return sg?-q:q;}uint r=(a+0xfffu+((a>>13)&1u))&0xffffe000u;return asfloat(sg|(r>=0x47800000u?0x7f800000u:r));}
float exact_half(double value){
 bool neg=value<0;double a=neg?-value:value;if(a>=65520.0)return asfloat(neg?0xff800000u:0x7f800000u);
 float approx=(float)a;int e=max(int((asuint(approx)>>23)&255u)-127,-14)-10;
 double scaled=a*(double)exp2(float(-e));uint lower=(uint)(float)scaled;if((double)lower>scaled)lower--;
 double remainder=scaled-(double)lower;if(remainder>0.5||(remainder==0.5&&(lower&1u)))lower++;
 return (neg?-1.0:1.0)*float(lower)*exp2(float(e));
}
[numthreads(64,1,1)]void merge(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/32,c=i%32;if(p>=width*height)return;
 uint low=((p/width)/2)*(width/2)+(p%width)/2;
 output[i]=H(H(input[low*32+c]*weights[c])+skip_or_color[i]*weights[32+c]);
}
[numthreads(64,1,1)]void finish(uint3 id:SV_DispatchThreadID){
 uint i=id.x+id.y*width*height,p=i/3,row=i%3;if(p>=width*height)return;float acc=0;
 [unroll]for(uint part=0;part<2;part++){
  float products[16];int e=acc==0?-1000:int((asuint(acc)>>23)&255u)-125;
  [unroll]for(uint j=0;j<16;j++){float a=input[p*32+part*16+j],b=weights[row*32+part*16+j];products[j]=a*b;if(products[j]!=0)e=max(e,int((asuint(a)>>23)&255u)+int((asuint(b)>>23)&255u)-252);}
  if(e!=-1000){float scale=asfloat(uint(27-e+127)<<23),quantum=asfloat(uint(e-27+127)<<23);int sum=0;
   [unroll]for(uint j=0;j<16;j++)sum+=(int)(products[j]*scale);
   double exact_sum=(double)float(sum>>16)*65536.0+(double)float(asuint(sum)&65535u);
   acc=exact_half(exact_sum*(double)quantum+(double)acc);
  }
 }
 // Explicit float32 boundaries, matching the original encode/add/decode order.
 float base=(float)((double)skip_or_color[p*4+row]*0.125-0.0625);
 float encoded=(float)((double)acc*(double)input_scale+(double)base);
 output[i]=clamp((float)((double)encoded*8.0+0.5),0.0,1.0);
}
