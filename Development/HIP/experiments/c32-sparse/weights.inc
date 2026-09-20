// Sparse contraction only. Bytes41088..43135 values;43136..44159 indices; float11040 flag.
inline void AppendC32Sparse(std::vector<float>&v){
 if(v.size()!=10272)throw std::runtime_error("sparse C32 expected residual tail");
 v.resize(11041,0.f);auto*bytes=reinterpret_cast<uint8_t*>(v.data());
 const char*env=std::getenv("DLSS5_C32_SPARSE");int mode=env?std::atoi(env):0;v[11040]=mode==1?1.f:0.f;
 for(unsigned tile=0;tile<2;tile++)for(unsigned chunk=0;chunk<4;chunk++)for(unsigned lane=0;lane<32;lane++){
  unsigned row=tile*16+lane%16,half=lane/16;uint32_t indices=0;
  for(unsigned g=0;g<4;g++){unsigned k=chunk*32+half*16+g*4;uint8_t q[4];float magnitude[4];
   for(unsigned j=0;j<4;j++){q[j]=bytes[4608*4+row*128+k+j];unsigned a=q[j]&127;magnitude[j]=a;}
   unsigned a=0;for(unsigned j=1;j<4;j++)if(magnitude[j]>magnitude[a])a=j;
   unsigned b=a==0?1:0;for(unsigned j=0;j<4;j++)if(j!=a&&magnitude[j]>magnitude[b])b=j;
   if(a>b){unsigned t=a;a=b;b=t;}
   bytes[41088+((tile*4+chunk)*32+lane)*8+g*2]=q[a];bytes[41088+((tile*4+chunk)*32+lane)*8+g*2+1]=q[b];indices|=(a|(b<<2))<<(4*g);
   if(mode==2)for(unsigned j=0;j<4;j++)if(j!=a&&j!=b)bytes[4608*4+row*128+k+j]=0;
  }
  std::memcpy(bytes+43136+((tile*4+chunk)*32+lane)*4,&indices,4);
 }
}
