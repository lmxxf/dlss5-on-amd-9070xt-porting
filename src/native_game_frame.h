#pragma once
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include <chrono>
#include "native_device_identity.h"
#include "native_game_rgb_input.h"
#include "native_actual_network70.h"
#include "native_rgb_texture.h"
#include "native_temporal_feed.h"
#include "native_temporal_coordinates.h"
#include "native_temporal_sample.h"
#include "native_submitted_readback.h"

// Integration boundary, not a ReShade callback. The caller must establish the
// correct source/color contract and submit all input producers before Process.
// Rebind rotating same-size source only after completed frames; recreate on resize.
// FAST/QUALITY PATH (DLSS5_OUTPUT_SMOOTH="t,s"): output-side temporal smoothing toward the warped previous output where the
// difference is below t/255 (blend weight s at zero difference, fading to 0 at t). In place on the network RGB buffer.
class NativeOutputSmooth {
 ID3D12Resource*rgb{};ID3D12Resource*warped{};ID3D12RootSignature*root{};ID3D12PipelineState*pso{};float threshold{},strength{};
public:
 bool enabled{};
 ~NativeOutputSmooth(){if(rgb)rgb->Release();if(warped)warped->Release();if(root)root->Release();if(pso)pso->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*network_rgb,ID3D12Resource*warped_history,const std::wstring&dir){
  const wchar_t*v=_wgetenv(L"DLSS5_OUTPUT_SMOOTH");if(!v||!*v)return;
  wchar_t*end=nullptr;threshold=float(wcstod(v,&end))/255.f;if(!end||*end!=L','||threshold<=0)throw std::runtime_error("invalid output smooth flag (t,s)");strength=float(wcstod(end+1,nullptr));if(strength<=0||strength>1)throw std::runtime_error("output smooth strength must be in (0,1]");
  rgb=network_rgb;rgb->AddRef();warped=warped_history;warped->AddRef();
  D3D12_ROOT_PARAMETER p[3]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,4};
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=p;ID3DBlob*blob=nullptr,*error=nullptr;if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)))throw std::runtime_error("output smooth root");if(error)error->Release();if(FAILED(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root))))throw std::runtime_error("output smooth root signature");blob->Release();blob=nullptr;error=nullptr;
  auto hr=CompileNativeShader(dir+L"\\native_output_smooth.hlsl",nullptr,"main",&blob,&error);if(FAILED(hr)){std::string m=error?std::string((const char*)error->GetBufferPointer(),error->GetBufferSize()):"output smooth compilation";if(error)error->Release();throw std::runtime_error(m);}if(error)error->Release();
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};if(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso))))throw std::runtime_error("output smooth pipeline");blob->Release();enabled=true;
 }
 // rgb is in SRV state after post70; transitioned to UAV for the pass and back. warped must be SRV-readable.
 void Record(ID3D12GraphicsCommandList*c){
  if(!enabled)return;const UINT pixels=1920*1080;UINT words[4];std::memcpy(words,&threshold,4);std::memcpy(words+1,&strength,4);words[2]=pixels;words[3]=0;
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={rgb,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};c->ResourceBarrier(1,&b);
  c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,warped->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(1,rgb->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(2,4,words,0);c->Dispatch((pixels+63)/64,1,1);
  std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);
 }
};
class NativeGameFrame {
 struct Resources {
  ID3D12Resource*original{}; // Kept alive by encode/decode resource references.
  NativeGameSubmission submit;
  NativeGameCodec encode;
  NativeGameRgbInput input;
  NativeActualNetwork70 network;
  NativeRgbTexture neural;NativeOutputSmooth smooth;
  NativeGameCodec decode;
  // Temporal path (optional): motion texture -> coordinates -> sampled history -> network temporal input.
  // Frame-side GPU probe (DLSS5_GAME_PROBE): pre-network passes / network / decode+copy per frame, averaged in the log.
  NativeNetworkTimestamps probe;bool probe_on{};double probe_sum[3]{};double probe_cpu{};unsigned probe_frames{},probe_history{},probe_reset{},probe_nomotion{};
  NativeTemporalFeed feed;NativeTemporalCoordinates coordinates;NativeTemporalSample sampler;ID3D12Resource*reciprocals{};bool temporal{};UINT motion_w{},motion_h{};ID3D12CommandQueue*queue{};
  UINT feed_motion_width()const{return motion_w;}UINT feed_motion_height()const{return motion_h;}ID3D12CommandQueue*submit_queue()const{return queue;}
  ~Resources(){if(reciprocals)reciprocals->Release();}
 };
public:
 struct TemporalConfig {UINT motion_width{},motion_height{},render_width{},render_height{};};
private:
 Resources*resources{};bool ready{},failed{};std::mutex mutex;
public:
 NativeGameFrame()=default;NativeGameFrame(const NativeGameFrame&)=delete;
 ~NativeGameFrame(){
  // Failure may mean an unfinished GPU submission. Retain the entire graph,
  // not just the command allocator, until process exit rather than risk UAF.
  if(!failed)delete resources;
 }
 void Create(ID3D12CommandQueue*queue,ID3D12Resource*source,
             const std::vector<float>&noise,const std::wstring&directory,ID3D12Resource*temporal_rgb=nullptr,const TemporalConfig*temporal_config=nullptr){
  std::lock_guard<std::mutex>guard(mutex);
  if(resources||!queue||!source)throw std::runtime_error("frame initialization contract");
  resources=new Resources;
  try{
   resources->submit.Create(queue);auto*d=resources->submit.Device();resources->queue=queue;
   {const wchar_t*pf=_wgetenv(L"DLSS5_GAME_PROBE");resources->probe_on=pf&&!wcscmp(pf,L"1");if(resources->probe_on)resources->probe.Create(d);}
   resources->encode.Create(d,{source},directory);
   resources->original=source;
   resources->input.Create(d,resources->encode.Output(),directory);
   if(temporal_config&&!temporal_rgb){
    // Motion vectors arrive in UV units of the render grid; the coordinate pass uses the captured
    // NGX contract (subrect 0,0..render extent over the motion texture; displacement scale 1/1920,1/1080).
    auto&t=*temporal_config;auto&r=*resources;
    r.feed.Create(d,t.motion_width,t.motion_height,1920.f,1080.f,directory);r.motion_w=t.motion_width;r.motion_h=t.motion_height;
    const float transform[6]={0,0,float(t.render_width),float(t.render_height),1.f/1920.f,1.f/1080.f};
    r.coordinates.Create(d,r.feed.Motion(),1920,1080,1920,1152,t.motion_width,t.motion_height,transform,directory,true);
    std::ifstream f((directory+L"\\normalized-output.f32").c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=33554432)throw std::runtime_error("reciprocal table missing");
    std::vector<float>table(8388608);f.seekg(0);if(!f.read(reinterpret_cast<char*>(table.data()),33554432))throw std::runtime_error("reciprocal table read");
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=33554432;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource*upload=nullptr;if(FAILED(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))))throw std::runtime_error("reciprocal upload");
    void*p=nullptr;D3D12_RANGE none{};if(FAILED(upload->Map(0,&none,&p)))throw std::runtime_error("reciprocal map");std::memcpy(p,table.data(),33554432);upload->Unmap(0,nullptr);
    r.reciprocals=NativeResidentTable(d,upload);
    r.sampler.Create(d,r.feed.History(),r.coordinates.Output(),1920,1080,1920*1152,directory,true,r.reciprocals);
    temporal_rgb=r.sampler.Output();r.temporal=true;
   }
   // Captured original post origin(-4,-4) corresponds to shift3.
   resources->network.Create(d,resources->input.Tiles(),resources->input.PostBase(),noise,directory,temporal_rgb,3);
   resources->neural.Create(d,resources->network.Output(),directory);
   if(resources->temporal)resources->smooth.Create(d,resources->network.Output(),resources->sampler.Output(),directory);
   if(resources->temporal)resources->feed.BindNetworkOutput(resources->network.Output());
   resources->decode.Create(d,{resources->encode.Output(),resources->neural.Output(),source},directory);ready=true;
  }catch(...){failed=true;throw;}
 }
 void RebindSourceAfterCompletion(ID3D12Resource*source){
  // ProcessSubmittedFrame holds this mutex through all completion waits. Failed
  // frames are never eligible: timeout does not mean GPU work has completed.
  std::lock_guard<std::mutex>guard(mutex);
  if(!ready||failed||!source)throw std::runtime_error("frame rebind unavailable");
  if(source==resources->original)return;
  try{resources->encode.RebindInputAfterCompletion(0,source);resources->decode.RebindInputAfterCompletion(2,source);resources->original=source;}
  catch(...){failed=true;throw;}
 }
 // Synchronizes encode -> network -> FP16 bridge -> decode -> FP16 copy on the
 // supplied queue. Both source and destination return to their original states.
 // Caller must serialize other users and supply the captured linear mode1 input.
 // This is NOT a swapchain present hook. Target is the game's float16 NR result.
 // Temporal sampler producer, if enabled, must already be submitted on this queue;
 // history provenance/reset policy remain the caller's responsibility.
 bool TemporalReady()const{return resources&&resources->temporal;}
 // Diagnostic: write the previous-output history buffer, the motion buffer and the current
 // encoded color (RGBA16F) to files so motion-vector sign/units can be checked offline.
 void RequestTemporalDump(const std::wstring&prefix){std::lock_guard<std::mutex>guard(mutex);dump_prefix=prefix;}
private:
 std::wstring dump_prefix;
 // Called inside ProcessSubmittedFrame after the input producers ran: history = previous output,
 // motion = this frame's vectors, color = this frame's encoded input, warped = sampler output.
 void DumpTemporalNow(const std::wstring&prefix){
  auto&r=*resources;auto*d=r.submit.Device();
  auto dump_buffer=[&](ID3D12Resource*src,UINT64 bytes,const std::wstring&name){
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   ID3D12Resource*rb=nullptr;if(FAILED(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb))))throw std::runtime_error("dump readback");
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,src,0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
   r.submit.Flush();void*p=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)},none{};if(FAILED(rb->Map(0,&range,&p)))throw std::runtime_error("dump map");
   FILE*f=_wfopen((prefix+name).c_str(),L"wb");if(f){fwrite(p,1,size_t(bytes),f);fclose(f);}rb->Unmap(0,&none);rb->Release();
  };
  dump_buffer(r.feed.History(),1920ull*1080*16,L"-history.f32");
  dump_buffer(r.feed.Motion(),UINT64(r.feed_motion_width())*r.feed_motion_height()*16,L"-motion.f32");
  dump_buffer(r.sampler.Output(),1920ull*1152*16,L"-warped.f32");
  auto color=NativeReadSubmittedFrame(r.submit_queue(),r.encode.Output(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  FILE*f=_wfopen((prefix+L"-color.rgba16f").c_str(),L"wb");if(f){fwrite(color.data(),1,color.size(),f);fclose(f);}
 }
public:
 // motion_texture: this frame's FSR motion vectors (compute-read state). reset: FFX reset flag.
 // History is the previous processed frame's network output; the first frame and reset frames run without it.
 void ProcessSubmittedFrame(ID3D12Resource*target,D3D12_RESOURCE_STATES source_state,
                            D3D12_RESOURCE_STATES target_state,UINT seed,bool temporal_enabled=false,ID3D12Resource*motion_texture=nullptr,bool reset=false){
  std::lock_guard<std::mutex>guard(mutex);
  if(!ready||failed||!target)throw std::runtime_error("frame unavailable");
  if(target==resources->original&&source_state!=target_state)throw std::runtime_error("aliased frame texture states disagree");
  auto desc=target->GetDesc();
  if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||desc.DepthOrArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)throw std::runtime_error("frame target must be1080p FP16");
  ID3D12Device*owner=nullptr;auto hr=target->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))throw std::runtime_error("frame target device query");bool same=NativeSameDevice(owner,resources->submit.Device());owner->Release();if(!same)throw std::runtime_error("frame target device mismatch");
  try{
   auto&r=*resources;
   const bool use_history=r.temporal&&motion_texture&&!reset&&r.feed.HasHistory();
   {static unsigned every=[]{const wchar_t*v=_wgetenv(L"DLSS5_MAKE_RESIDENT_EVERY");return v?unsigned(wcstoul(v,nullptr,10)):0u;}();static unsigned frames=0;static unsigned fails=0;
    if(every&&++frames%every==0){HRESULT mr=NativeMakeAllResident(r.submit.Device());if(FAILED(mr)&&++fails<=5)if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-submission-order.txt").c_str(),L"ab")){fprintf(f,"pid=%lu make_resident_failed hr=%08x tracked=%u\n",GetCurrentProcessId(),unsigned(mr),unsigned(NativeTrackedResources().size()));fclose(f);}}}
   const auto cpu_start=std::chrono::steady_clock::now();if(r.probe_on)r.probe.Reset();
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){if(r.probe_on)r.probe.Mark(c,"t0");r.encode.Record(c,{source_state});r.input.Record(c,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(use_history){r.feed.RecordMotion(c,motion_texture);r.coordinates.Record(c);r.sampler.Record(c);}if(r.probe_on)r.probe.Mark(c,"t1");});
   if(!dump_prefix.empty()&&use_history){r.submit.Flush();DumpTemporalNow(dump_prefix);dump_prefix.clear();}
   r.network.Run(r.submit,seed,r.temporal?use_history:temporal_enabled);
   r.submit.Submit([&](ID3D12GraphicsCommandList*c){if(r.probe_on)r.probe.Mark(c,"t2");
    if(use_history)r.smooth.Record(c);
    if(r.temporal)r.feed.RecordHistory(c);
    r.neural.Record(c);r.decode.Record(c,{D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,source_state});
    D3D12_RESOURCE_BARRIER b[2]{};for(auto&v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition={r.decode.Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,target_state,D3D12_RESOURCE_STATE_COPY_DEST};
    c->ResourceBarrier(1,b);if(target_state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
    c->CopyResource(target,r.decode.Output());
    for(auto&v:b)std::swap(v.Transition.StateBefore,v.Transition.StateAfter);
    c->ResourceBarrier(1,b);if(target_state!=D3D12_RESOURCE_STATE_COPY_DEST)c->ResourceBarrier(1,b+1);
    if(r.probe_on){r.probe.Mark(c,"t3");r.probe.Resolve(c);}
   });
   if(r.probe_on){r.submit.Flush();std::vector<double>iv;if(r.probe.Intervals(r.submit.TimestampFrequency(),iv)&&iv.size()==3){for(int i=0;i<3;i++)r.probe_sum[i]+=iv[i];}
    r.probe_cpu+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_start).count();
    if(use_history)r.probe_history++;if(reset)r.probe_reset++;if(!motion_texture)r.probe_nomotion++;
    if(++r.probe_frames%100==0){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-probe.txt").c_str(),L"ab")){fprintf(f,"frames=%u avg_ms pre=%.2f network=%.2f post=%.2f gpu_total=%.2f cpu_frame=%.2f history=%u reset=%u nomotion=%u\n",r.probe_frames,r.probe_sum[0]/100,r.probe_sum[1]/100,r.probe_sum[2]/100,(r.probe_sum[0]+r.probe_sum[1]+r.probe_sum[2])/100,r.probe_cpu/100,r.probe_history,r.probe_reset,r.probe_nomotion);fclose(f);}for(auto&v:r.probe_sum)v=0;r.probe_cpu=0;r.probe_history=r.probe_reset=r.probe_nomotion=0;}}
  }catch(...){failed=true;throw;}
 }
};
