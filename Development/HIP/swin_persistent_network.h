#include "swin_persistent_types.h"
struct SpPlan {
 std::vector<SpLayer> layers;
 std::vector<SpNode> nodes;
 std::vector<U> offsets;
 std::vector<Tensor> outputs;
 Tensor gpu_nodes,state;
 U first=0,total=0,next_ticket=0;
 bool w16=false; // C256 through sp_run256_w16/sp_recover256_w16 with the @ffn-frag-w16 weights
};
std::map<std::tuple<U,U,U,U>,SpPlan> sp_plans;
bool sp_disabled=false;bool sp_ends_head=false,sp_ends_tail=false;
bool SpLoadModule(){if(modules.count("sp"))return true;Handle m{};if(api.LoadModule(&m,(opt.modules+"/swin-persistent.hsaco").c_str()))return false;modules["sp"]=m;return true;}
bool SpEndsCapable(){return HasFn("sp","sp_ends_capable");}
void *sp_error_allocation=nullptr;std::atomic<U>*sp_error_host=nullptr;U*sp_error_device=nullptr;
int(*sp_host_free)(void*)=nullptr;
unsigned long long sp_runs=0,sp_fallbacks=0,sp_rollovers=0,sp_jobs=0;
#ifndef HIP_SWIN_PERSISTENT_DIAGNOSTICS
#define HIP_SWIN_PERSISTENT_DIAGNOSTICS 0
#endif
static U SpEnv(const char *key,U fallback=0){
#if HIP_SWIN_PERSISTENT_DIAGNOSTICS
 const char*p=std::getenv(key);if(p&&*p)return U(std::stoull(p));
#endif
 return fallback;
}
static U SpShift(U block){if(block>=40)return Shift(block);static constexpr U masks[]={0,3,1,2};U first=block>=15?15:block>=9?9:5;return masks[(block-first)%4];}
bool SpEnabled(U c,bool decoder){
 if(!sp_disabled&&sp_error_host&&sp_error_host[0].load(std::memory_order_acquire)){
 sp_disabled=true;const char*msg="DLSS5 Swin ready-queue timeout/error: GPU serial recovery used; persistent disabled for this instance.\n";
 std::fputs(msg,stderr);OutputDebugStringA(msg);
 }
 U bit=c==64?1:c==128?2:c==256?4:0;
 U sides=SpEnv("SP_SIDES",3);
 return !sp_disabled&&wave_owned_active&&opt.pooled&&!opt.graph&&!observer&&opt.dump_dir.empty()&&
   (SpEnv("SP_CHANNELS",SwinRunCompatible(opt)?4u:0u)&bit)&&(sides&(decoder?2u:1u));
}
SpPlan &SpGetPlan(U w,U h,U c,U first,U layers){
 auto key=std::make_tuple(w,h,c,first);auto it=sp_plans.find(key);
 if(it!=sp_plans.end())return it->second;
 SpPlan plan;plan.offsets.push_back(0);plan.next_ticket=SpEnv("SP_TICKET_START");
 if(layers>8)throw std::runtime_error("SP layer limit");
 if(!sp_error_allocation){
  int(*device_pointer)(void**,void*,unsigned)=nullptr;
  api.Load(device_pointer,"hipHostGetDevicePointer");api.Load(sp_host_free,"hipHostFree");
  api.Check(api.hipHostMalloc(&sp_error_allocation,2*sizeof(U),2),"SP mapped errors");
  sp_error_host=static_cast<std::atomic<U>*>(sp_error_allocation);
  new(sp_error_host)std::atomic<U>(0);new(sp_error_host+1)std::atomic<U>(0);
  api.Check(device_pointer(reinterpret_cast<void**>(&sp_error_device),sp_error_allocation,0),"SP mapped error device pointer");
 }
 if(!modules.count("sp")){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/swin-persistent.hsaco").c_str()),"SP module");modules["sp"]=m;}
 plan.w16=HIP_C256_FFN_W16&&c==256&&HasFn("sp","sp_run256_w16")&&HasFn("sp","sp_recover256_w16");
 for(U i=0;i<layers;i++){
  U b=first+i,shift=SpShift(b),sx=(shift&1)?4:0,sy=(shift&2)?4:0,ww=(w+sx+7)&~7u,hh=(h+sy+7)&~7u;
  /* SP_ENDS: sp_ends_head/tail mark the chain's f32 head input / f32 raw tail output (results/c256-gap-20261001) */
  const U mode=(i==0&&sp_ends_head?1u:0u)|(i+1==layers&&sp_ends_tail?2u:0u);
  auto out=New(size_t(w)*h*c/((mode&2)?1:4));
  plan.layers.push_back({nullptr,plan.w16?PackedFusedMhWeightFragW16(Block(b,"ffn"),c):PackedFusedMhWeightFrag(Block(b,"ffn"),c),WaveOwnedAttentionWeight(Block(b,"attention"),c),P(out),w,h,ww,hh,sx,sy,(mode&2)?3u:4u,mode});
  plan.outputs.push_back(out);
  U count=ww/8*(hh/8);
  for(U j=0;j<count;j++)plan.nodes.push_back({i,j,0,0,{0,0,0,0}});
  plan.offsets.push_back(U(plan.nodes.size()));
 }
 plan.first=plan.offsets[1];plan.total=U(plan.nodes.size());
 if(!plan.total||plan.total>1000000)throw std::runtime_error("SP task count");
 // A window reads/writes exactly its clipped raster rectangle. Every next
 // window overlapping it receives one dependency; at most 2x2 windows.
 for(U i=0;i+1<layers;i++){
  const auto&a=plan.layers[i];const auto&b=plan.layers[i+1];
  for(U id=plan.offsets[i];id<plan.offsets[i+1];id++){
   auto&node=plan.nodes[id];int wx=int(node.window%(a.ww/8)),wy=int(node.window/(a.ww/8));
   int x0=std::max(0,wx*8-int(a.sx)),y0=std::max(0,wy*8-int(a.sy));
   int x1=std::min(int(w),wx*8-int(a.sx)+8),y1=std::min(int(h),wy*8-int(a.sy)+8);
   if(x0>=x1||y0>=y1)throw std::runtime_error("SP empty window");
   for(int y=(y0+int(b.sy))/8;y<=(y1-1+int(b.sy))/8;y++)
    for(int x=(x0+int(b.sx))/8;x<=(x1-1+int(b.sx))/8;x++){
     U child=plan.offsets[i+1]+U(y)*(b.ww/8)+U(x);
     if(child>=plan.offsets[i+2]||node.nchild>=4)throw std::runtime_error("SP edge range");
     node.child[node.nchild++]=child;plan.nodes[child].need++;
    }
  }
 }
 for(U j=plan.first;j<plan.total;j++)if(plan.nodes[j].need<1||plan.nodes[j].need>4)throw std::runtime_error("SP indegree");
 plan.gpu_nodes=New((plan.nodes.size()*sizeof(SpNode)+3)/4);
 plan.state=New(4+3*size_t(plan.total));
 api.Check(api.hipMemcpy(P(plan.gpu_nodes),plan.nodes.data(),plan.nodes.size()*sizeof(SpNode),1),"SP nodes upload");
 std::printf("SP_PLAN c=%u first=%u layers=%u tasks=%u initial=%u state_bytes=%zu w16=%u\n",c,first,layers,plan.total,plan.first,plan.state->bytes,unsigned(plan.w16));
 return sp_plans.emplace(key,std::move(plan)).first->second;
}
Tensor SpStage(Tensor input,U w,U h,U c,U first,U layers){
 auto &plan=SpGetPlan(w,h,c,first,layers);
 // Standard stream launches form a real boundary with the preceding PDL chain.
 pdl_prev={};pdl_ffn_flags=nullptr;pdl_anyorder=false;
 U limit=SpEnv("SP_TICKET_LIMIT",std::numeric_limits<U>::max());
 if(limit<plan.total)limit=plan.total;
 if(plan.next_ticket>limit-plan.total){
  api.Check(api.hipStreamSynchronize(stream),"SP rollover drain");
  api.Check(api.hipMemsetAsync(P(plan.state),0,plan.state->bytes,stream),"SP rollover clear");
  api.Check(api.hipStreamSynchronize(stream),"SP rollover ready");
  plan.next_ticket=0;sp_rollovers++;
 }
 for(U i=0;i<layers;i++){
  plan.layers[i].in=i?P(plan.outputs[i-1]):P(input);
  if(plan.layers[i].in==plan.layers[i].out)throw std::runtime_error("SP input/output alias");
 }
 // Outputs are separate per layer. The original input is never overwritten,
 // so timeout recovery can replay the whole stage with the original kernels.
 // By-value kernel arguments avoid mutable asynchronous host descriptor data.
 SpParams p{};std::copy(plan.layers.begin(),plan.layers.end(),p.layers);
 p.nodes=static_cast<const SpNode*>(P(plan.gpu_nodes));p.state=static_cast<U*>(P(plan.state));p.error_host=sp_error_device;
 p.total=plan.total;p.first=plan.first;p.base=plan.next_ticket;p.fault=SpEnv("SP_FORCE_TIMEOUT");p.layers_count=layers;p.timeout_ticks=10000000ull;
 void *args[]={&p};
#ifdef HIP_SWIN_PERSISTENT_TRACE
 std::printf("TOPO,sp,sp_init,%u,%u,256\n",3*plan.total,U((3*size_t(plan.total)+255)/256));
 std::printf("TOPO,sp,sp_run%u,%u,%u,%u\n",c,plan.total,plan.total,c);
 std::printf("TOPO,sp,sp_recover%u,1,1,%u\n",c,c);
#endif
 api.Check(api.hipModuleLaunchKernel(Fn("sp","sp_init"),U((3*size_t(plan.total)+255)/256),1,1,256,1,1,0,stream,args,nullptr),"SP init");
 const char*sfx=plan.w16?"_w16":"";
 std::string kernel="sp_run"+std::to_string(c)+sfx;
 auto start=std::chrono::steady_clock::now();
 api.Check(api.hipModuleLaunchKernel(Fn("sp",kernel),plan.total,1,1,c,1,1,0,stream,args,nullptr),"SP run");
 std::string recovery="sp_recover"+std::to_string(c)+sfx;
 api.Check(api.hipModuleLaunchKernel(Fn("sp",recovery),1,1,1,c,1,1,0,stream,args,nullptr),"SP bounded recovery");
 plan.next_ticket+=plan.total;sp_runs++;sp_jobs+=plan.total;
 if(SpEnv("SP_VALIDATE")||SpEnv("SP_TRACE")){
  api.Check(api.hipStreamSynchronize(stream),"SP diagnostic completion");
  U status[4]{};api.Check(api.hipMemcpy(status,P(plan.state),sizeof(status),2),"SP diagnostic status");
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  bool failed=status[2]!=0;
  if(!failed&&(status[0]!=plan.next_ticket||status[1]!=plan.next_ticket))throw std::runtime_error("SP terminal ticket mismatch");
  if(!failed&&SpEnv("SP_VALIDATE")){
  std::vector<U>state(4+3*plan.total);api.Check(api.hipMemcpy(state.data(),P(plan.state),state.size()*4,2),"SP diagnostic state");
  std::vector<U>seen(plan.total,0);
  for(U i=0;i<plan.first;i++)seen[i]++;
  for(U i=plan.first;i<plan.total;i++){U value=state[4+plan.total+i];if(!value||value>plan.total)failed=true;else seen[value-1]++;}
  for(U i=0;i<plan.total;i++)if(seen[i]!=1||state[4+i]!=plan.nodes[i].need)failed=true;
   if(failed)throw std::runtime_error("SP queue permutation or dependency mismatch");
  }
  if(status[2]){
   sp_disabled=true;
   std::printf("SP_GPU_FALLBACK c=%u first=%u code=%u timeouts=%u head=%u tail=%u ms=%.3f\n",c,first,status[2],status[3],status[0],status[1],ms);
  }else if(SpEnv("SP_TRACE"))std::printf("SP_DONE c=%u first=%u tasks=%u base=%u ms=%.3f\n",c,first,plan.total,p.base,ms);
 }
 Stage("persistent-"+std::to_string(first),plan.outputs.back());return plan.outputs.back();
}
void SpReport(){
 U errors=sp_error_host?sp_error_host[0].load(std::memory_order_acquire):0;
 sp_fallbacks=sp_error_host?sp_error_host[1].load(std::memory_order_acquire):0;
 std::printf("SP_STATS runs=%llu fallback=%llu rollover=%llu jobs=%llu disabled=%u errors=%u\n",sp_runs,sp_fallbacks,sp_rollovers,sp_jobs,unsigned(sp_disabled||errors),errors);
 sp_plans.clear();if(sp_error_allocation){sp_host_free(sp_error_allocation);sp_error_allocation=nullptr;sp_error_host=nullptr;sp_error_device=nullptr;}
}
