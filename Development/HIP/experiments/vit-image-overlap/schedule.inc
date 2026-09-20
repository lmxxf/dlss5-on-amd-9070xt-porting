 // Schedule image-only diagnostics ahead of the encoder; R3 kernel math/decisions unchanged.
 Handle image_aux{},image_ready{},image_done{};bool image_prepared=false,image_pending=false;
 using ImageWaitFn=int(*)(Handle,Handle,unsigned);ImageWaitFn image_wait{};
 using ImageEventFn=int(*)(Handle*,unsigned);ImageEventFn image_event{};
 using ImageStreamFn=int(*)(Handle*,unsigned);ImageStreamFn image_stream{};
 void StopImageSchedule(){
  api.hipStreamSynchronize(stream);if(image_aux)api.hipStreamSynchronize(image_aux);
  if(image_ready)api.hipEventDestroy(image_ready);if(image_done)api.hipEventDestroy(image_done);if(image_aux)api.hipStreamDestroy(image_aux);
  image_ready=image_done=image_aux=nullptr;
 }
 void JoinImageSchedule(){if(image_pending){api.Check(image_wait(stream,image_done,0),"image join");image_pending=false;}}
 void BeginImageSchedule(){
  image_prepared=false;if(image_pending)throw std::runtime_error("unjoined image work");
  const char*v=std::getenv("DLSS5_VIT_IMAGE_SCHEDULE");unsigned kind=v?unsigned(std::stoul(v)):0;if(kind>2)throw std::runtime_error("image schedule0..2");
  const char*m=std::getenv("DLSS5_VIT_ADAPTIVE");
  if(!kind||!m||strcmp(m,"1")||adaptive_user_disabled||adaptive_last_mode!=1||!adaptive_state||adaptive_dirty||opt.graph||opt.profile||opt.wall_profile||observer||!opt.dump_dir.empty())return;
  if(std::chrono::steady_clock::now()-adaptive_last>std::chrono::milliseconds(500))return;
  const U tiles=((W+31)/32)*((H+31)/32);Handle main=stream;
  if(kind==2){
   if(!image_aux){api.Load(image_wait,"hipStreamWaitEvent");api.Load(image_event,"hipEventCreateWithFlags");api.Load(image_stream,"hipStreamCreateWithFlags");
    api.Check(image_stream(&image_aux,1),"image nonblocking stream");api.Check(image_event(&image_ready,2),"image ready event");api.Check(image_event(&image_done,2),"image done event");}
   // Main already has the D3D input fence wait and the previous anchor commit in order.
   api.Check(api.hipEventRecord(image_ready,main),"image input ready");api.Check(image_wait(image_aux,image_ready,0),"image input wait");stream=image_aux;
  }
  try{Run("deep","reuse_image_stats",size_t(tiles)*256,adaptive_image,P(adaptive_image_anchor),P(adaptive_state),P(adaptive_image_signature),P(adaptive_image_delta),W,H);}
  catch(...){stream=main;throw;}stream=main;
  if(kind==2){api.Check(api.hipEventRecord(image_done,image_aux),"image completion");image_pending=true;}
  image_prepared=true;
 }
