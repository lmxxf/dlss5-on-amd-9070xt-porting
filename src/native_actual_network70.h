#pragma once
#include "native_device_identity.h"
#include "native_c32_ds.h"
#include "native_vit_block.h"
#include "native_actual_decoder69.h"
#include "native_post70.h"
#include "native_block_skip.h"
#include "native_vram_log.h"
#include "native_game_submission.h"
#include "native_network_timestamps.h"
#include <functional>

// Actual processing extent, with externally supplied GPU RGB tiles and HWC base.
// No fixture activations, oracles, game command lists, or CPU pixel readbacks.
class NativeActualNetwork70 {
 NativeMatrixWorkspace matrix_workspace;bool share_matrix{};
 NativePreblockRuntime pre;
 NativeC32Stage c32[4];NativeC64Shift c64[4],c128[6],c256[8];
 NativeC32Downsample ds4,ds8,ds14,ds22,head;
 NativeSplitWindow split[8];NativeVitGather bridge;NativeVitBlock vit[8];
 NativeActualDecoder69 decoder;NativePost70 post;
 UINT batch_submits{}; // 0 = one list per chunk, 1 = per ViT layer / 3 decoder stages, 2 = 4 ViT layers / half the decoder per list
 ID3D12Device*device{};bool ready{},failed{},temporal_bound{};
 NativeNetworkTimestamps timestamps;bool profile{};
 // DLSS5_TEST_ISOLATE=<kind>[:index[:part]][,...] (test only): after the frame is read back, re-record one stage isolate_repeat times
 // between two timestamps; total/repeat is the stage's real GPU cost without top-of-pipe timestamp attribution. Output is garbage afterwards.
 std::string isolate;UINT isolate_repeat{200};NativeNetworkTimestamps isolate_timestamps;
 static std::vector<float>Read(const std::wstring&path){
  std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("network coefficient missing");auto n=f.tellg();if(n<=0||size_t(n)%4)throw std::runtime_error("network coefficient size");std::vector<float>v(size_t(n)/4);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),n))throw std::runtime_error("network coefficient truncated");return v;
 }
public:
 NativeActualNetwork70()=default;NativeActualNetwork70(const NativeActualNetwork70&)=delete;
 ~NativeActualNetwork70(){if(device)device->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*rgb_tiles,ID3D12Resource*rgb_hwc,
             const std::vector<float>&noise,const std::wstring&dir,ID3D12Resource*temporal_rgb=nullptr,UINT post_shift=0){
  if(post_shift>3)throw std::runtime_error("network post shift");
  if(device||!d||!rgb_tiles||!rgb_hwc||noise.size()!=201326592/4)throw std::runtime_error("network initialization contract");
  if(_wgetenv(L"DLSS5_POST_BASE_ONLY"))throw std::runtime_error("diagnostic post forbidden");
  for(auto*r:{rgb_tiles,rgb_hwc,temporal_rgb}){
   if(!r)continue;
   if(r->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||r->GetDesc().Width<1920ull*1152*16)throw std::runtime_error("network RGB capacity");
   ID3D12Device*owner=nullptr;auto hr=r->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))throw std::runtime_error("network RGB device query");bool same=NativeSameDevice(owner,d);owner->Release();if(!same)throw std::runtime_error("network RGB device mismatch");
  }
  device=d;device->AddRef();auto read=[&](const std::wstring&name){return Read(dir+L"\\"+name);};
  if(const wchar_t*v=_wgetenv(L"DLSS5_TEST_ISOLATE")){for(;*v;v++)isolate+=char(*v);isolate_timestamps.Create(d);if(const wchar_t*r=_wgetenv(L"DLSS5_TEST_ISOLATE_REPEAT")){wchar_t*end=nullptr;auto n=wcstoul(r,&end,10);if(!*r||*end||n<1||n>2000)throw std::runtime_error("isolate repeat must be 1..2000");isolate_repeat=UINT(n);}}
  if(const wchar_t*v=_wgetenv(L"DLSS5_NETWORK_GPU_PROFILE")){if(wcscmp(v,L"1"))throw std::runtime_error("invalid network profile flag");profile=true;timestamps.Create(d);}
  if(const wchar_t*s=_wgetenv(L"DLSS5_TEST_SHARED_MATRIX_WORKSPACE")){if(wcscmp(s,L"0")&&wcscmp(s,L"1"))throw std::runtime_error("invalid shared matrix workspace flag");share_matrix=!wcscmp(s,L"1");if(share_matrix)matrix_workspace.Create(d,(_wgetenv(L"DLSS5_TEST_MATRIX_C64")&&!wcscmp(_wgetenv(L"DLSS5_TEST_MATRIX_C64"),L"1"))?488ull*296*64:248ull*152*128);}
  NativeVramLog(d,"before");pre.Create(d,rgb_tiles,1920,1152,read(L"block0-ffn.f32"),read(L"block0-attention.f32"),dir,true,false,&noise,temporal_rgb);temporal_bound=temporal_rgb!=nullptr;
  NativeVramLog(d,"pre");const UINT shifts[]={0,3,1,2,0,3,1,2};auto*source=pre.Downsample();
  const wchar_t*c32_mapped=_wgetenv(L"DLSS5_C32_MAPPED_INPUT");if(c32_mapped&&wcscmp(c32_mapped,L"0")&&wcscmp(c32_mapped,L"1")&&wcscmp(c32_mapped,L"2"))throw std::runtime_error("invalid mapped C32 flag");
  const wchar_t*cr=_wgetenv(L"DLSS5_C32_CHAIN_RAW");if(cr&&wcscmp(cr,L"0")&&wcscmp(cr,L"1"))throw std::runtime_error("invalid C32 chain raw flag");const bool chain_raw=cr&&!wcscmp(cr,L"1");
  // FAST PATH (DLSS5_C32_SKIP8): block 4 finish writes E4M3 main8 (no f32 main, no crop); the block 66 projection reads it as the skip residual.
  bool skip8=false;{const wchar_t*s8=_wgetenv(L"DLSS5_C32_SKIP8");if(s8&&wcscmp(s8,L"0")&&wcscmp(s8,L"1"))throw std::runtime_error("invalid C32 skip8 flag");skip8=s8&&!wcscmp(s8,L"1");if(skip8&&!(_wgetenv(L"DLSS5_PREBLOCK_MAIN8")&&!wcscmp(_wgetenv(L"DLSS5_PREBLOCK_MAIN8"),L"1")))throw std::runtime_error("C32 skip8 needs DLSS5_PREBLOCK_MAIN8");}
  for(UINT i=0;i<4;i++){auto p=L"block"+std::to_wstring(i+1);NativePreblockRuntime::PendingMain8()=skip8&&i==3;c32[i].Create(d,source,960,576,shifts[i],read(p+L"-ffn.f32"),read(p+L"-attention.f32"),dir,false,c32_mapped&&(!wcscmp(c32_mapped,L"1")||(!wcscmp(c32_mapped,L"2")&&i==0)));
   if(c32_mapped&&!wcscmp(c32_mapped,L"1")){if(i){if(chain_raw){c32[i].ChainFromRaw(c32[i-1]);c32[i-1].SetSkipFinish(true);}else c32[i].ChainFrom(c32[i-1]);}else c32[i].MapFromRaster(source);if(i)c32[i-1].SetCropNeeded(false);}else if(c32_mapped&&!wcscmp(c32_mapped,L"2")&&i==0)c32[i].MapFromRaster(source);
   source=(chain_raw&&i<3)?c32[i].RawWork():c32[i].Output();} // chained stages: the next Create only needs a resource for its unused SRV
  NativePreblockRuntime::PendingMain8()=false;if(skip8){if(!c32[3].Main8())throw std::runtime_error("C32 skip8: block 4 has no main8 finish");if(!_wgetenv(L"DLSS5_TEST_SKIP8_PROBE"))c32[3].SetCropNeeded(false);/* probe: keep the crop */NativeVitLinear::PendingSkip8()=NativeSkip8Source{c32[3].Main8(),c32[3].WorkWidth(),c32[3].ShiftX(),c32[3].ShiftY()};}
  NativeVramLog(d,"c32x4");ds4.Create(d,c32[3].PooledWork(),960,576,2,read(L"block4-ds.f32"),dir);source=ds4.Output();
  auto group=[&](NativeC64Shift*layers,UINT count,UINT first,UINT w,UINT h,UINT channels,NativeC32Downsample&ds){
   for(UINT i=0;i<count;i++){auto p=L"block"+std::to_wstring(first+i);layers[i].Create(d,source,w,h,shifts[i],read(p+L"-ffn.f32"),read(p+L"-attention.f32"),dir,i+1==count,channels,false,share_matrix?&matrix_workspace:nullptr,i>0,i+1<count);source=layers[i].Output();}
   ds.Create(d,source,w,h,0,read(L"block"+std::to_wstring(first+count-1)+L"-ds.f32"),dir,true,channels,share_matrix?&matrix_workspace:nullptr);source=ds.Output();
  };
  group(c64,4,5,480,288,64,ds8);NativeVramLog(d,"c64x4");group(c128,6,9,240,144,128,ds14);NativeVramLog(d,"c128x6");group(c256,8,15,120,72,256,ds22);NativeVramLog(d,"c256x8");
  for(UINT i=0;i<8;i++){auto p=L"block"+std::to_wstring(23+i);split[i].Create(d,source,60,36,shifts[i],read(p+L"-ffwd.f32"),read(p+L"-ffwd-projection.f32"),read(p+L"-attention.f32"),dir,i==7,share_matrix?&matrix_workspace:nullptr);source=split[i].Output();NativeVramLog(d,i?"split":"split0");}
  head.Create(d,source,60,36,0,read(L"head-matrix.f32"),dir,true,512,share_matrix?&matrix_workspace:nullptr);
  NativeVramLog(d,"head");auto rawmap=read(L"hwc-to-vit.i32");if(rawmap.size()!=655360)throw std::runtime_error("network bridge map size");std::vector<UINT>map(rawmap.size());std::memcpy(map.data(),rawmap.data(),map.size()*4);bridge.Create(d,head.Output(),map,dir);source=bridge.Output();
  for(UINT i=0;i<8;i++){auto p=L"block"+std::to_wstring(31+i)+L"-";vit[i].Create(d,source,640,read(p+L"expand.f32"),read(p+L"contract.f32"),read(p+L"qkv.f32"),read(p+L"projection.f32"),dir);source=vit[i].Output();NativeVramLog(d,"vit");}
  // FAST PATH (DLSS5_POST70_LOW_RAW): 1 = post70 reads block 69's raw tiles (Ffast; block 69 skips finish+crop; post FFN +0.45ms, not worth it);
  // 2 = block 69 finish writes E4M3 main8 (no f32 main, no crop) and post70 reads the bytes (mode 9).
  UINT low_raw=0;{const wchar_t*lr=_wgetenv(L"DLSS5_POST70_LOW_RAW");if(lr&&wcscmp(lr,L"0")&&wcscmp(lr,L"1")&&wcscmp(lr,L"2"))throw std::runtime_error("invalid post70 low raw flag");low_raw=lr?UINT(lr[0]-L'0'):0u;if(low_raw&&!pre.Main8Mode())throw std::runtime_error("post70 low raw needs DLSS5_PREBLOCK_MAIN8");}
  NativeDecoderTail69::PendingLastMain8()=low_raw==2;
  NativeVramLog(d,"c512+vit");decoder.Create(d,source,split[7].Output(),c256[7].Output(),c128[5].Output(),c64[3].Output(),skip8?c32[3].RawWork():c32[3].Output(),dir,share_matrix?&matrix_workspace:nullptr);
  {const wchar_t*bs=_wgetenv(L"DLSS5_BATCH_SUBMITS");if(bs&&wcscmp(bs,L"0")&&wcscmp(bs,L"1")&&wcscmp(bs,L"2"))throw std::runtime_error("invalid batch submits flag");batch_submits=bs?UINT(bs[0]-L'0'):0u;}
  auto&last=decoder.Tail().Last();if(low_raw==1){last.SetSkipFinish(true);last.SetCropNeeded(false);}if(low_raw==2){if(!last.Main8())throw std::runtime_error("post70 low raw 2: block 69 has no main8 finish");last.SetCropNeeded(false);}
  NativeVramLog(d,"decoder");post.Create(d,low_raw==1?last.RawWork():low_raw==2?last.Main8():decoder.Output(),pre.DownOnly()?pre.RawTiles():pre.Main8Mode()?pre.Main8():pre.Main(),rgb_hwc,1920,1152,read(L"post70-scales.f32"),read(L"post70-ffn.f32"),read(L"post70-attention.f32"),read(L"post70-head.f32"),dir,.03125f,post_shift,pre.DownOnly()?6u:pre.Main8Mode()?7u:4u,pre.DownOnly()?pre.WorkWidth():0u,low_raw?last.WorkWidth():0u,low_raw?last.ShiftX():0u,low_raw?last.ShiftY():0u,low_raw==2?9u:8u);NativeVramLog(d,"post70");ready=true;
 }
 // Caller serializes whole frames and must retain this object after GPU timeout.
 // Input producer MUST already have been submitted to the same queue.
 // This includes temporal_rgb's sampler producer when temporal_enabled is true.
 // Binding storage does not imply a valid history frame; reset callers pass false.
 template<class Submission> void Run(Submission&submit,UINT seed,bool temporal_enabled=false){
  if(!ready||failed||!NativeSameDevice(submit.Device(),device))throw std::runtime_error("network unavailable/device mismatch");
  if(temporal_enabled&&!temporal_bound)throw std::runtime_error("network temporal RGB not bound");
  try{
   timestamps.Reset();
   submit.Submit([&](ID3D12GraphicsCommandList*c){
    timestamps.Mark(c,"start");pre.Record(c,seed,false,temporal_enabled,profile?&timestamps:nullptr,"preblock_detail");timestamps.Mark(c,"preblock");
    for(UINT i=0;i<4;i++){c32[i].Record(c,profile&&i==0?&timestamps:nullptr);if(profile)timestamps.Mark(c,"enc_c32_"+std::to_string(i));}ds4.Record(c);timestamps.Mark(c,"encoder1_4");
    auto run=[&](auto&layer,UINT block,NativeNetworkTimestamps*t){if(NativeSkipBlock(block))NativeSkipCopy(c,layer.Input(),layer.Output(),block);else layer.Record(c,t);};
    for(UINT i=0;i<4;i++)run(c64[i],5+i,profile&&i==0?&timestamps:nullptr);ds8.Record(c);timestamps.Mark(c,"encoder5_8");
    for(UINT i=0;i<6;i++)run(c128[i],9+i,nullptr);ds14.Record(c);timestamps.Mark(c,"encoder9_14");
    for(UINT i=0;i<8;i++)run(c256[i],15+i,nullptr);ds22.Record(c);timestamps.Mark(c,"encoder15_22");
    for(UINT i=0;i<8;i++)run(split[i],23+i,profile&&i==0?&timestamps:nullptr);timestamps.Mark(c,"encoder23_30_body");head.Record(c);timestamps.Mark(c,"encoder_head");bridge.Record(c);timestamps.Mark(c,"encoder23_head");
   });
   // FAST PATH (DLSS5_BATCH_SUBMITS): one command list per ViT layer and per few decoder stages instead of one per chunk
   // (~100 lists/frame -> ~25); the in-list barriers already order the dispatches. CPU recording overhead, not GPU time.
   const bool batch=batch_submits>0;const UINT vit_per_list=batch_submits>=2?4u:1u,decoder_per_list=batch_submits>=2?(decoder.StageCount()+1)/2:3u;
   auto record_vit=[&](ID3D12GraphicsCommandList*c,UINT b){auto&layer=vit[b];for(UINT stage=0;stage<5;stage++)for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++){layer.RecordStageChunk(c,stage,chunk);if(chunk+1==layer.StageChunks(stage))timestamps.Mark(c,"vit"+std::to_string(31+b)+"_stage"+std::to_string(stage));}};
   if(batch)for(UINT b0=0;b0<8;b0+=vit_per_list)submit.Submit([&](ID3D12GraphicsCommandList*c){for(UINT b=b0;b<b0+vit_per_list;b++){if(NativeSkipBlock(31+b))NativeSkipCopy(c,vit[b].Input(),vit[b].Output(),31+b);else record_vit(c,b);}});
   else for(UINT b=0;b<8;b++){auto&layer=vit[b];
    for(UINT stage=0;stage<5;stage++)for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++)submit.Submit([&](ID3D12GraphicsCommandList*c){layer.RecordStageChunk(c,stage,chunk);if(chunk+1==layer.StageChunks(stage))timestamps.Mark(c,"vit"+std::to_string(31+b)+"_stage"+std::to_string(stage));});}
   if(batch){const UINT n=decoder.StageCount();for(UINT s0=0;s0<n;s0+=decoder_per_list)submit.Submit([&](ID3D12GraphicsCommandList*c){for(UINT stage=s0;stage<std::min(n,s0+decoder_per_list);stage++){if(stage==12)timestamps.Mark(c,"decoder_tail_begin");decoder.RecordStage(c,stage,profile?&timestamps:nullptr);timestamps.Mark(c,"decoder_stage"+std::to_string(stage));}});}
   else for(UINT stage=0;stage<decoder.StageCount();stage++)submit.Submit([&](ID3D12GraphicsCommandList*c){if(stage==12)timestamps.Mark(c,"decoder_tail_begin");decoder.RecordStage(c,stage,profile?&timestamps:nullptr);timestamps.Mark(c,"decoder_stage"+std::to_string(stage));});
   submit.Submit([&](ID3D12GraphicsCommandList*c){post.Record(c,profile?&timestamps:nullptr);timestamps.Mark(c,"post70");timestamps.Resolve(c);});
   if(profile){submit.Flush();timestamps.Report(submit.TimestampFrequency());}
  }catch(...){failed=true;throw;}
 }
 // For an already-recording external command list only. Does not close/reset,
 // submit, or wait. Caller owns ordering, GPU lifetime and TDR budgeting.
 // This batches the same stages as Run; single-list GPU safety is not implied
 // by the separately submitted/chunked tests and must be verified separately.
 void RecordUnsubmitted(ID3D12GraphicsCommandList*c,UINT seed,bool temporal_enabled=false){
  // Whole-network batching produced DXGI_ERROR_DEVICE_HUNG on9070XT.
  // Keep only as an explicit diagnostic; never silently enable in game code.
  const wchar_t*permit=_wgetenv(L"DLSS5_TEST_SINGLE_LIST");
  if(!permit||wcscmp(permit,L"1"))throw std::runtime_error("single-list network is diagnostic-only: observed device hang");
  if(!c)throw std::runtime_error("network null command list");
  ID3D12Device*owner=nullptr;auto hr=c->GetDevice(IID_PPV_ARGS(&owner));
  if(FAILED(hr))throw std::runtime_error("network command list device query");
  bool same=NativeSameDevice(owner,device);owner->Release();if(!same)throw std::runtime_error("network command list device mismatch");
  InlineRecorder recorder{device,c};Run(recorder,seed,temporal_enabled);
 }
 bool Isolating()const{return!isolate.empty();}
 // See the isolate comment above. kinds: pre | c32:i | c64:i[:part] | c128:i[:part] | c256:i[:part] | ds:{4,8,14,22} | split:i | head | bridge | vit:b[:stage] | decoder:stage | tail:block[:part] (49..69) | tailproj:{56,62,66} | post
 // part (multihead body): 1 pack 2 expand 3 contract 4 proj0 5 qkv 6 normalize 7 attention 8 proj1; barriers are kept, other dispatches skipped.
 template<class Submission> void Isolate(Submission&submit,UINT seed){
  if(isolate.empty())return;size_t pos=0;while(pos<=isolate.size()){size_t comma=isolate.find(',',pos);std::string item=isolate.substr(pos,comma==std::string::npos?std::string::npos:comma-pos);pos=comma==std::string::npos?isolate.size()+1:comma+1;if(item.empty())continue;IsolateOne(submit,seed,item);}
 }
 template<class Submission> void IsolateOne(Submission&submit,UINT seed,const std::string&isolate){
  std::string kind=isolate;UINT index=0,part=0;size_t colon=kind.find(':');if(colon!=std::string::npos){std::string rest=kind.substr(colon+1);kind=kind.substr(0,colon);size_t c2=rest.find(':');if(c2!=std::string::npos){part=UINT(std::stoul(rest.substr(c2+1)));rest=rest.substr(0,c2);}index=UINT(std::stoul(rest));}
  auto multihead=[&](NativeC64Shift*layers,UINT count){if(index>=count)throw std::runtime_error("isolate index");layers[index].SetIsolatePart(part);return [&,index](ID3D12GraphicsCommandList*c){layers[index].Record(c);};};
  std::function<void(ID3D12GraphicsCommandList*)>record;
  if(kind=="pre"){pre.SetIsolateStage(part);record=[&](ID3D12GraphicsCommandList*c){pre.Record(c,seed,false,false);};}
  else if(kind=="c32"){if(index>=4)throw std::runtime_error("isolate index");record=[&](ID3D12GraphicsCommandList*c){c32[index].Record(c);};}
  else if(kind=="c64")record=multihead(c64,4);else if(kind=="c128")record=multihead(c128,6);else if(kind=="c256")record=multihead(c256,8);
  else if(kind=="ds"){NativeC32Downsample*d=index==4?&ds4:index==8?&ds8:index==14?&ds14:index==22?&ds22:nullptr;if(!d)throw std::runtime_error("isolate ds index");record=[d](ID3D12GraphicsCommandList*c){d->Record(c);};}
  else if(kind=="split"){if(index>=8)throw std::runtime_error("isolate index");record=[&](ID3D12GraphicsCommandList*c){split[index].Record(c);};}
  else if(kind=="head")record=[&](ID3D12GraphicsCommandList*c){head.Record(c);};
  else if(kind=="bridge")record=[&](ID3D12GraphicsCommandList*c){bridge.Record(c);};
  else if(kind=="vit"){if(index>=8)throw std::runtime_error("isolate index");record=[&,colon](ID3D12GraphicsCommandList*c){auto&layer=vit[index];bool one=isolate.find(':',colon+1)!=std::string::npos;for(UINT stage=0;stage<5;stage++){if(one&&stage!=part)continue;for(UINT chunk=0;chunk<layer.StageChunks(stage);chunk++)layer.RecordStageChunk(c,stage,chunk);}};}
  else if(kind=="decoder"){if(index>=decoder.StageCount())throw std::runtime_error("isolate index");record=[&](ID3D12GraphicsCommandList*c){decoder.RecordStage(c,index);};}
  else if(kind=="tail"){decoder.Tail().SetIsolatePart(index,part);record=[&,index](ID3D12GraphicsCommandList*c){decoder.Tail().RecordBlock(c,index);};}
  else if(kind=="tailproj")record=[&,index](ID3D12GraphicsCommandList*c){decoder.Tail().RecordProjection(c,index);};
  else if(kind=="post"){post.SetIsolatePart(part);record=[&](ID3D12GraphicsCommandList*c){post.Record(c);};}
  else throw std::runtime_error("isolate kind: "+kind);
  isolate_timestamps.Reset();
  submit.Submit([&](ID3D12GraphicsCommandList*c){isolate_timestamps.Mark(c,"isolate_start");D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;for(UINT r=0;r<isolate_repeat;r++){record(c);c->ResourceBarrier(1,&b);}isolate_timestamps.Mark(c,"isolate_end");isolate_timestamps.Resolve(c);});
  submit.Flush();std::vector<double>ms;if(!isolate_timestamps.Intervals(submit.TimestampFrequency(),ms)||ms.size()!=1)throw std::runtime_error("isolate timestamps");
  std::printf("network_isolate stage=%s repeat=%u total_ms=%.3f per_ms=%.4f\n",isolate.c_str(),isolate_repeat,ms[0],ms[0]/isolate_repeat);std::fflush(stdout);
  for(auto*l:{c64,c128,c256})for(UINT i=0;i<8&&!(l==c64&&i>=4)&&!(l==c128&&i>=6);i++)l[i].SetIsolatePart(0);
  if(kind=="tail")decoder.Tail().SetIsolatePart(index,0);pre.SetIsolateStage(0);post.SetIsolatePart(0);
 }
 ID3D12Resource*Output()const{return post.Output();}
 /* test only (DLSS5_TEST_DUMP_BLOCK4): block 4 finish outputs for CPU comparison */
 ID3D12Resource*Block4Main8()const{return c32[3].Main8();}ID3D12Resource*Project66Output(){return decoder.Tail().Project66Output();}ID3D12Resource*Block4Down()const{return c32[3].PooledWork();}ID3D12Resource*SharedRawScratch()const{return c32[3].RawWork();}ID3D12Resource*SharedFfnScratch()const{return c32[3].FfnScratch();}ID3D12Resource*Block4Main()const{return c32[3].Main8()?nullptr:c32[3].MainF32();}
 ID3D12Resource*Head()const{return head.Output();}
 ID3D12Resource*Decoder69()const{return decoder.Output();}
private:
 struct InlineRecorder {
  ID3D12Device*device;ID3D12GraphicsCommandList*commands;
  ID3D12Device*Device()const{return device;}
  UINT64 TimestampFrequency()const{throw std::runtime_error("inline recorder cannot report completed timestamps");}
  void Flush(){}
  template<class F>void Submit(F&&record){record(commands);}
 };
};
