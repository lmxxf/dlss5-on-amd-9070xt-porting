 Tensor adaptive_anchor_in,adaptive_anchor_out,adaptive_gain,adaptive_stats,adaptive_state;
 bool adaptive_active=false;U adaptive_n=0;std::chrono::steady_clock::time_point adaptive_last{};
 Tensor AdaptiveVitGroup(Tensor input,U n){
  const char*mode_s=std::getenv("DLSS5_VIT_ADAPTIVE");U mode=mode_s?U(std::stoul(mode_s)):0;
  if(!mode)return ResidualVitGroup(input,n);
  if(mode>3||opt.graph||!opt.fast_deep||!opt.pooled||opt.vit_byte_stream||std::any_of(opt.skip_blocks.begin(),opt.skip_blocks.end(),[](U b){return b>=31&&b<=38;}))throw std::runtime_error("adaptive ViT requires fast pooled graph-off non-byte-stream unskipped network, mode1..3");
  auto param=[](const char*key,float fallback){const char*v=std::getenv(key);float f=v?std::stof(v):fallback;if(!std::isfinite(f)||f<0)throw std::runtime_error("adaptive threshold");return f;};
  float global=param("DLSS5_VIT_REUSE_GLOBAL",.20f),local=param("DLSS5_VIT_REUSE_LOCAL",1.f);
  U period=4;if(const char*v=std::getenv("DLSS5_VIT_REUSE_PERIOD"))period=U(std::stoul(v));if(period<1||period>16)throw std::runtime_error("adaptive max period1..16");
  auto now=std::chrono::steady_clock::now();bool reset=!adaptive_state||adaptive_n!=n||(adaptive_last.time_since_epoch().count()&&now-adaptive_last>std::chrono::milliseconds(500));adaptive_last=now;
  if(!adaptive_gain){const char*path=std::getenv("DLSS5_VIT_REUSE_GAIN");if(!path||!*path)throw std::runtime_error("adaptive gain file missing");auto bytes=ReadBytes(path);if(bytes.size()!=4096)throw std::runtime_error("adaptive gain shape");for(U i=0;i<1024;i++){float v;memcpy(&v,bytes.data()+4*i,4);if(!std::isfinite(v))throw std::runtime_error("adaptive gain nonfinite");}adaptive_gain=Upload(bytes.data(),bytes.size(),true);}
  if(reset){U zero[8]{};adaptive_state=Upload(zero,sizeof(zero),true);adaptive_anchor_in=New(size_t(n)*1024);adaptive_anchor_out=New(size_t(n)*1024);adaptive_stats=New(size_t(n)*3);adaptive_n=n;}
  U stride=W==1920?32:W/64;
  Run("deep","reuse_token_stats",size_t(n)*256,P(input),P(adaptive_anchor_in),P(adaptive_state),P(adaptive_stats),n,W/64,H/64,stride);
  Run("deep","reuse_decide",256,P(adaptive_stats),P(adaptive_state),n,period,global,local,mode);
  Tensor full=input;adaptive_active=true;
  try{for(U block=31;block<=38;block++)full=Vit(full,n,block);}catch(...){adaptive_active=false;throw;}adaptive_active=false;
  auto result=New(size_t(n)*1024);Run("deep","reuse_finish",size_t(n)*1024,P(input),P(full),P(adaptive_anchor_in),P(adaptive_anchor_out),P(adaptive_gain),P(adaptive_state),P(result),U(n*1024));
  if(const char*log=std::getenv("DLSS5_VIT_ADAPTIVE_LOG")){if(*log){Synchronize();U state[8];api.Check(api.hipMemcpy(state,P(adaptive_state),sizeof(state),2),"adaptive diagnostic");float g,l;memcpy(&g,state+4,4);memcpy(&l,state+5,4);FILE*f=fopen(log,"ab");if(!f)throw std::runtime_error("adaptive log open");fprintf(f,"%u,%u,%u,%u,%.9g,%.9g\n",state[3],state[0],state[1],state[6],g,l);fclose(f);}}
  return result;
 }
