 // Experiment B: allocations/exports prepared at construction, no reset/upload after external input wait.
 bool chain_lab=false,chain_requested=false,chain_enabled=false,chain_overflow=false,chain_ordered=false;
 int chain_force_slot=-1;std::string chain_trace_dir;unsigned chain_prod_stride=0,chain_cons_stride=0;
 unsigned chain_tiles=0,chain_wait_limit=16384;unsigned long long chain_calls=0,chain_anyorder_calls=0,chain_ordered_calls=0;
 std::array<unsigned long long,16>chain_targets{};
 std::array<std::array<Tensor,2>,16>chain_keep{};
 Tensor chain_ready,chain_status,chain_prod_trace,chain_cons_trace;
 decltype(ext_launch)chain_ext_launch=nullptr;
 static int C512ChainSlot(U block){return block>=23&&block<=30?int(block-23):block>=40&&block<=47?int(block-40+8):-1;}
 unsigned long long*ChainCounter(int slot){return static_cast<unsigned long long*>(P(chain_ready))+size_t(slot)*chain_tiles;}
 unsigned long long*ChainProducerTrace(int slot){return chain_prod_trace?static_cast<unsigned long long*>(P(chain_prod_trace))+size_t(slot)*chain_prod_stride:nullptr;}
 unsigned long long*ChainConsumerTrace(int slot){return chain_cons_trace?static_cast<unsigned long long*>(P(chain_cons_trace))+size_t(slot)*chain_cons_stride:nullptr;}
 unsigned long long ChainWaitTarget(int slot)const{return chain_targets[slot]+(slot==chain_force_slot?8ull:0ull);}
 void InitC512Chain(){
  const char*mode=std::getenv("NR_C512_CHAIN");chain_lab=mode!=nullptr;chain_requested=mode&&std::strcmp(mode,"1")==0;
  if(!chain_lab)return;
  const bool shape=(W==1600&&H==960)||(W==1920&&(H==1088||H==1152));
  if(!shape||small_active||!fast_numeric||multi_pass!=1||opt.graph||!opt.pooled||opt.sparse_weights||opt.experimental_temporal||!opt.skip_blocks.empty()||!opt.dup_prefix.empty()||opt.wall_profile||opt.profile)return;
  chain_tiles=(W/32*(H/32)+15)/16;
  if(const char*v=std::getenv("NR_C512_CHAIN_ORDERED"))chain_ordered=std::strcmp(v,"1")==0;
  if(const char*v=std::getenv("NR_C512_CHAIN_FORCE_TIMEOUT")){chain_force_slot=std::stoi(v);if(chain_force_slot<0||chain_force_slot>=16)throw std::runtime_error("chain force slot outside0..15");}
  if(const char*v=std::getenv("NR_C512_CHAIN_TRACE_DIR")){chain_trace_dir=v;if(!chain_trace_dir.empty())std::filesystem::create_directories(std::filesystem::u8path(chain_trace_dir));}

  chain_ext_launch=reinterpret_cast<decltype(chain_ext_launch)>(GetProcAddress(api.dll,"hipExtModuleLaunchKernel"));
  const bool exports=chain_ext_launch&&HasFn("c512_m32_deep","split_ffn_one_w2f8_w16_bin_chain")&&HasFn("deep_fast","split_projection_frag_w16_bskip_chain");
  if(!exports){std::fprintf(stderr,"C512_CHAIN unavailable=exports_or_extension requested=%u\n",unsigned(chain_requested));return;}
  unsigned long long start=0;if(const char*v=std::getenv("NR_C512_CHAIN_START")){start=std::stoull(v);if(start%8)throw std::runtime_error("chain start must be multiple of 8");}
  if(const char*v=std::getenv("NR_C512_CHAIN_WAIT")){chain_wait_limit=unsigned(std::stoul(v));if(!chain_wait_limit||chain_wait_limit>16384)throw std::runtime_error("chain wait outside 1..16384");}
  chain_ready=std::make_shared<Allocation>(api,size_t(16)*chain_tiles*8);chain_status=std::make_shared<Allocation>(api,4);
  std::vector<unsigned long long>initial(size_t(16)*chain_tiles,start);unsigned zero=0;
  api.Check(api.hipMemcpy(P(chain_ready),initial.data(),initial.size()*8,1),"chain initial counters");api.Check(api.hipMemcpy(P(chain_status),&zero,4,1),"chain initial status");
  if(chain_force_slot>=0&&start>std::numeric_limits<unsigned long long>::max()-16)throw std::runtime_error("forced timeout target would overflow");
  if(!chain_trace_dir.empty()){
   chain_prod_stride=((chain_tiles+1)/2)*16;chain_cons_stride=chain_tiles*8;
   chain_prod_trace=std::make_shared<Allocation>(api,size_t(16)*chain_prod_stride*8);chain_cons_trace=std::make_shared<Allocation>(api,size_t(16)*chain_cons_stride*8);
   std::vector<unsigned long long>zero_trace(size_t(16)*std::max(chain_prod_stride,chain_cons_stride),0);
   api.Check(api.hipMemcpy(P(chain_prod_trace),zero_trace.data(),size_t(16)*chain_prod_stride*8,1),"chain initial producer trace");api.Check(api.hipMemcpy(P(chain_cons_trace),zero_trace.data(),size_t(16)*chain_cons_stride*8,1),"chain initial consumer trace");
  }
  chain_targets.fill(start);chain_enabled=chain_requested;
  std::fprintf(stderr,"C512_CHAIN init requested=%u enabled=%u tiles=%u start=%llu wait=%u\n",unsigned(chain_requested),unsigned(chain_enabled),chain_tiles,start,chain_wait_limit);
 }
 bool PrepareC512Chain(U block,U n,bool eligible){
  const int slot=C512ChainSlot(block);
  if(!chain_enabled||slot<0||!eligible||small_active||!fast_numeric||multi_pass!=1||opt.graph||!opt.pooled||pdl_anyorder||!opt.dup_prefix.empty()||opt.wall_profile||opt.profile||n/16!=chain_tiles||n%16||C512W16Mode()<=0||(VitInterByteMode()&16))return false;
  if(chain_targets[slot]>std::numeric_limits<unsigned long long>::max()-(slot==chain_force_slot?16ull:8ull)){chain_enabled=false;chain_overflow=true;return false;}
  chain_targets[slot]+=8;++chain_calls;return true;
 }
 public:
 // Caller has already flushed the frame; clear only references. Candidate retention/release is part of its measured cost.
 void ReleaseChainFrame(){if(!chain_requested)return;for(auto&pair:chain_keep){pair[0].reset();pair[1].reset();}}
 void CheckChainStatus(){
  if(!chain_lab)return;api.Check(api.hipSetDevice(int(opt.device)),"chain check owner");unsigned status=0;api.Check(api.hipStreamSynchronize(stream),"chain final drain");
  if(chain_status)api.Check(api.hipMemcpy(&status,P(chain_status),4,2),"chain final status");
  std::vector<unsigned long long>ready(chain_ready?size_t(16)*chain_tiles:0);size_t mismatches=0;
  if(chain_ready){api.Check(api.hipMemcpy(ready.data(),P(chain_ready),ready.size()*8,2),"chain final ready");for(unsigned slot=0;slot<16;slot++)for(unsigned t=0;t<chain_tiles;t++)mismatches+=ready[size_t(slot)*chain_tiles+t]!=chain_targets[slot];}
  std::fprintf(stderr,"C512_CHAIN final requested=%u enabled=%u calls=%llu anyorder=%llu ordered=%llu overflow=%u status=%u counter_values=%zu counter_mismatches=%zu\n",unsigned(chain_requested),unsigned(chain_enabled),chain_calls,chain_anyorder_calls,chain_ordered_calls,unsigned(chain_overflow),status,ready.size(),mismatches);
  if(status||mismatches)throw std::runtime_error("C512 chain timeout/counter mismatch: whole experiment rejected");
  if(chain_prod_trace&&chain_cons_trace){
   std::vector<unsigned long long>pt(size_t(16)*chain_prod_stride),ct(size_t(16)*chain_cons_stride);
   api.Check(api.hipMemcpy(pt.data(),P(chain_prod_trace),pt.size()*8,2),"chain trace producers");api.Check(api.hipMemcpy(ct.data(),P(chain_cons_trace),ct.size()*8,2),"chain trace consumers");
   std::ofstream f(std::filesystem::u8path(chain_trace_dir)/"chain-trace.json");if(!f)throw std::runtime_error("chain trace output open");
   f<<"{\"clock\":\"gfx12 realtime message131 ticks\",\"ordered\":"<<(chain_ordered?1:0)<<",\"tiles\":"<<chain_tiles<<",\"producer_stride\":"<<chain_prod_stride<<",\"consumer_stride\":"<<chain_cons_stride<<",\"edges\":[";
   for(unsigned s=0;s<16;s++){if(s)f<<',';f<<"{\"slot\":"<<s<<",\"block\":"<<(s<8?s+23:s-8+40)<<",\"target\":"<<chain_targets[s]<<",\"producer_prepublish\":[";for(unsigned i=0;i<chain_prod_stride;i++){if(i)f<<',';f<<pt[size_t(s)*chain_prod_stride+i];}f<<"],\"consumer_first_mma_complete\":[";for(unsigned i=0;i<chain_cons_stride;i++){if(i)f<<',';f<<ct[size_t(s)*chain_cons_stride+i];}f<<"],\"ready\":[";for(unsigned i=0;i<chain_tiles;i++){if(i)f<<',';f<<ready[size_t(s)*chain_tiles+i];}f<<"]}";}f<<"]}\n";if(!f)throw std::runtime_error("chain trace output write");
  }
 }
 private:
