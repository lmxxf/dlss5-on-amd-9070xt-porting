 Tensor adaptive_anchor_in,adaptive_anchor_out,adaptive_gain,adaptive_stats,adaptive_state;
 Tensor adaptive_image_anchor,adaptive_image_signature,adaptive_image_delta;void*adaptive_image=nullptr;
 void*adaptive_prev_history=nullptr;void*adaptive_prev_input=nullptr;U adaptive_prev_seed=0;
 bool adaptive_dirty=false,adaptive_key_down=false,adaptive_user_disabled=false;U adaptive_last_mode=0,adaptive_frame=0;
 bool adaptive_active=false;U adaptive_n=0;std::chrono::steady_clock::time_point adaptive_last{};
 Tensor AdaptiveVitGroup(Tensor input,U n){
  const char*mode_s=std::getenv("DLSS5_VIT_ADAPTIVE");U mode=mode_s?U(std::stoul(mode_s)):0;
  const char*hotkey=std::getenv("DLSS5_VIT_REUSE_HOTKEY");
  if(hotkey&&!strcmp(hotkey,"1")){bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;if(down&&!adaptive_key_down)adaptive_user_disabled=!adaptive_user_disabled;adaptive_key_down=down;if(adaptive_user_disabled)mode=0;}
  AdaptivePreviewState.store(mode?1:0);if(mode!=adaptive_last_mode){adaptive_dirty=true;adaptive_last_mode=mode;}
  if(!mode){Tensor full=input;for(U b=31;b<=38;b++)full=Vit(full,n,b);return full;}++adaptive_frame;
  if(mode>3||opt.graph||!opt.fast_deep||!opt.pooled||opt.vit_byte_stream||std::any_of(opt.skip_blocks.begin(),opt.skip_blocks.end(),[](U b){return b>=31&&b<=38;}))throw std::runtime_error("adaptive ViT requires fast pooled graph-off non-byte-stream unskipped network, mode1..3");
  auto param=[](const char*key,float fallback){const char*v=std::getenv(key);float f=v?std::stof(v):fallback;if(!std::isfinite(f)||f<0)throw std::runtime_error("adaptive threshold");return f;};
  float global=param("DLSS5_VIT_REUSE_GLOBAL",.22f),local=param("DLSS5_VIT_REUSE_LOCAL",1.f),image_limit=param("DLSS5_VIT_REUSE_IMAGE",.35f);
  U period=4;if(const char*v=std::getenv("DLSS5_VIT_REUSE_PERIOD"))period=U(std::stoul(v));if(period<1||period>16)throw std::runtime_error("adaptive max period1..16");
  auto now=std::chrono::steady_clock::now();bool reset=adaptive_dirty||!adaptive_state||adaptive_n!=n||(adaptive_last.time_since_epoch().count()&&now-adaptive_last>std::chrono::milliseconds(500));adaptive_last=now;adaptive_dirty=false;
  if(!adaptive_gain){const char*path=std::getenv("DLSS5_VIT_REUSE_GAIN");if(!path||!*path)throw std::runtime_error("adaptive gain file missing");auto bytes=ReadBytes(path);if(bytes.size()!=4096)throw std::runtime_error("adaptive gain shape");for(U i=0;i<1024;i++){float v;memcpy(&v,bytes.data()+4*i,4);if(!std::isfinite(v))throw std::runtime_error("adaptive gain nonfinite");}adaptive_gain=Upload(bytes.data(),bytes.size(),true);}
  if(reset){U zero[8]{};adaptive_state=Upload(zero,sizeof(zero),true);adaptive_anchor_in=New(size_t(n)*1024);adaptive_anchor_out=New(size_t(n)*1024);adaptive_stats=New(size_t(n)*4);adaptive_n=n;U tiles=((W+31)/32)*((H+31)/32);adaptive_image_anchor=New(size_t(tiles)*3);adaptive_image_signature=New(size_t(tiles)*3);adaptive_image_delta=New(tiles);}
  U stride=W==1920?32:W/64,tiles=((W+31)/32)*((H+31)/32);
  if(!adaptive_image)throw std::runtime_error("adaptive image missing");
  Run("deep","reuse_image_stats",size_t(tiles)*256,adaptive_image,P(adaptive_image_anchor),P(adaptive_state),P(adaptive_image_signature),P(adaptive_image_delta),W,H);
  Run("deep","reuse_token_stats",size_t(n)*256,P(input),P(adaptive_anchor_in),P(adaptive_state),P(adaptive_stats),n,W/64,H/64,stride);
  Run("deep","reuse_decide",256,P(adaptive_stats),P(adaptive_state),n,period,global,local,mode,P(adaptive_image_delta),tiles,(W+31)/32,image_limit);
  Tensor full=input;adaptive_active=true;
  try{for(U block=31;block<=38;block++)full=Vit(full,n,block);}catch(...){adaptive_active=false;throw;}adaptive_active=false;
  auto result=New(size_t(n)*1024);Run("deep","reuse_finish",size_t(n)*1024,P(input),P(full),P(adaptive_anchor_in),P(adaptive_anchor_out),P(adaptive_gain),P(adaptive_state),P(result),U(n*1024),P(adaptive_image_signature),P(adaptive_image_anchor),U(tiles*3));
  if(const char*log=std::getenv("DLSS5_VIT_ADAPTIVE_LOG")){if(*log){Synchronize();U state[8];api.Check(api.hipMemcpy(state,P(adaptive_state),sizeof(state),2),"adaptive diagnostic");float g,l,image_peak;memcpy(&image_peak,state+7,4);memcpy(&g,state+4,4);memcpy(&l,state+5,4);FILE*f=fopen(log,"ab");if(!f)throw std::runtime_error("adaptive log open");fprintf(f,"%u,%u,%u,%u,%.9g,%.9g,%.9g\n",adaptive_frame,state[0],state[1],state[6],g,l,image_peak);fclose(f);}}
  return result;
 }
