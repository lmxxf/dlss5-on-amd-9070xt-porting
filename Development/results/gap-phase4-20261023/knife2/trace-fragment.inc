 // CPU dispatch receipt only: no observer, dump, profile, or GPU timing instrumentation.
 bool small_trace_enabled=false;unsigned small_trace_frame=0;std::vector<std::string>small_trace;unsigned long long small_trace_vp=0,small_trace_vc=0;
 template<class F,class...A>auto SmallTraceLaunch(const char*kind,F fn,Handle h,A...a)->decltype(fn(h,a...)){
  if(small_trace_enabled){std::string n="UNMAPPED";for(const auto&kv:functions)if(kv.second==h){n=kv.first;break;}small_trace.push_back(std::string(kind)+" "+n);}
  return fn(h,a...);
 }
 public:
 void SmallTraceBegin(unsigned frame,unsigned expected_tokens){if(!small_active||EffectiveModelMode()!=0||MultiPass()!=1)throw std::runtime_error("trace requires actual Small31");if(SmallVitWidth()*SmallVitHeight()!=expected_tokens||!opt.skip_blocks.empty())throw std::runtime_error("trace actual Small token/full-graph mismatch");small_trace_frame=frame;small_trace.clear();small_trace_vp=vfrag_producer_calls;small_trace_vc=vfrag_consumer_calls;small_trace_enabled=true;}
 void SmallTraceEnd(){
  small_trace_enabled=false;size_t weights_bytes=0,pool_bytes=0;for(const auto&x:weights)if(x.second&&x.second->owned)weights_bytes+=x.second->capacity;for(const auto&x:pool)if(x&&x->owned)pool_bytes+=x->capacity;
  std::printf("SMALL_TRACE_PROFILE frame=%u W=%u H=%u requested=%u effective=%u small_active=%u blocks=%u deep_w=%u deep_h=%u vit_w=%u vit_h=%u tokens=%u compact_contract=%u compact_projection=%u contract_w16=%u projection_w16=%u contract_bstream=%u projection_bout=%u entry_exit_eligible=%u c512_byte_eligible=%u weights_count=%zu weights_bytes=%zu pool_count=%zu pool_bytes=%zu vfrag_producers=%llu vfrag_consumers=%llu vfrag_producer_delta=%llu vfrag_consumer_delta=%llu\n",small_trace_frame,W,H,ModelMode(),EffectiveModelMode(),unsigned(small_active),small_active?31u:71u,DeepWidth(),DeepHeight(),SmallVitWidth(),SmallVitHeight(),SmallVitWidth()*SmallVitHeight(),unsigned(compact_contract_fn!=nullptr),unsigned(compact_projection_fn!=nullptr),unsigned(compact_contract_w16),unsigned(compact_projection_w16),unsigned(compact_contract_bstream_fn!=nullptr),unsigned(compact_projection_bout_fn!=nullptr),unsigned(VitEntryExitByteOk()),unsigned(C512ByteStreamOk()),weights.size(),weights_bytes,pool.size(),pool_bytes,vfrag_producer_calls,vfrag_consumer_calls,vfrag_producer_calls-small_trace_vp,vfrag_consumer_calls-small_trace_vc);
  size_t packs=0,up39=0,unmapped=0;
  for(size_t i=0;i<small_trace.size();i++){const auto&name=small_trace[i];packs+=name.find(":vit_pack_input")!=std::string::npos;up39+=name.find(":decoder_project2x_h16w_w_bin")!=std::string::npos;unmapped+=name.find("UNMAPPED")!=std::string::npos;std::printf("SMALL_TRACE_DISPATCH frame=%u index=%zu %s\n",small_trace_frame,i,name.c_str());}
  std::printf("SMALL_TRACE_COUNT frame=%u dispatches=%zu vit_pack_input=%zu up39_f32_byte_input=%zu unmapped=%zu\n",small_trace_frame,small_trace.size(),packs,up39,unmapped);
  if(unmapped)throw std::runtime_error("trace contains unmapped actual kernel handle");
 }
 private:
