   unsigned chain_warm_runs=0;
   auto pool_bytes=[&](){size_t total=0;for(const auto&t:pool)total+=t->capacity;return total;};
   if(chain_requested){
    unsigned stable=0,cycles=0;const bool extra_ae=saved_adaptive&&ae&&strcmp(ae,"0");
    while(chain_warm_runs+(extra_ae?2u:1u)<=8){
     const size_t before_pool=pool.size(),before_bytes=pool_bytes();const auto before_uploads=weight_uploads;
     Enqueue(scratch,hist,out,0);Synchronize();ReleaseChainFrame();++chain_warm_runs;
     if(extra_ae){SetAdaptiveReuseAllowed(false);Enqueue(scratch,hist,out,0);Synchronize();ReleaseChainFrame();++chain_warm_runs;SetAdaptiveReuseAllowed(saved_adaptive);}
     ++cycles;const size_t after_pool=pool.size(),after_bytes=pool_bytes();
     const bool unchanged=after_pool==before_pool&&after_bytes==before_bytes&&weight_uploads==before_uploads;
     stable=unchanged?stable+1:0;
     std::fprintf(stderr,"C512_CHAIN warm runs=%u cycle=%u profile=%u pool=%zu->%zu bytes=%zu->%zu uploads=%llu->%llu stable=%u enabled=%u overflow=%u before_producer_wait=1\n",chain_warm_runs,cycles,profile,before_pool,after_pool,before_bytes,after_bytes,before_uploads,weight_uploads,stable,unsigned(chain_enabled),unsigned(chain_overflow));
     if(stable>=2)break;
    }
    if(stable<2)throw std::runtime_error("C512 chain cold pool did not stabilize in 8 warm runs");
    std::fprintf(stderr,"C512_CHAIN warm_ready runs=%u cycles=%u profile=%u pool=%zu bytes=%zu uploads=%llu enabled=%u overflow=%u before_producer_wait=1\n",chain_warm_runs,cycles,profile,pool.size(),pool_bytes(),weight_uploads,unsigned(chain_enabled),unsigned(chain_overflow));
   }else{
    Enqueue(scratch,hist,out,0);Synchronize();
    if(saved_adaptive&&ae&&strcmp(ae,"0")){SetAdaptiveReuseAllowed(false);Enqueue(scratch,hist,out,0);Synchronize();SetAdaptiveReuseAllowed(saved_adaptive);}
   }
