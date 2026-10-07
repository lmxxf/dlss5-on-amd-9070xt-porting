#pragma once
#include "native_fast_history.h"
#include "native_game_submission.h"
#include "native_lab_paths.h"
#include "native_temporal_experiment.h"
#include <array>
#include <fstream>

// Addon policy around the reusable shader helper. A slot owns immutable frame
// constants and guide views until the output submission fence completes.
class NativeAddonFastHistory {
 struct Slot {
  Microsoft::WRL::ComPtr<ID3D12Resource> control,motion,depth;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
  UINT64 fence{};
 };
 std::array<Slot,64> slots{}; unsigned next{};
 NativeFastHistory::History history;
 NativeFastHistory::Parameters parameters{};
 bool prior{}; unsigned frame{},seed{}; float exposure{1};
 Slot* current{};
public:
 bool enabled{};
 static std::vector<float> Row(const std::wstring&directory){
  std::ifstream f(directory+L"\\post70-history-head.f16",std::ios::binary|std::ios::ate);
  if(!f||f.tellg()!=64)throw std::runtime_error("fast history requires post70-history-head.f16 (32 half coefficients)");
  uint16_t halves[32];f.seekg(0);if(!f.read(reinterpret_cast<char*>(halves),64))throw std::runtime_error("fast history row read");
  std::vector<float> row;for(auto h:halves)row.push_back(NativeHalfToFloat(h));return row;
 }
 void Create(ID3D12Device*d,UINT w,UINT h,UINT ph,UINT rw,UINT rh,UINT64 offset,ID3D12Resource*shared){
  if(!rw||!rh||offset%4||offset/4>UINT_MAX)throw std::runtime_error("fast history geometry");
  history.Create(d,w,h,ph,shared);parameters.width=w;parameters.height=h;parameters.processingHeight=ph;
  parameters.viewWidth=w;parameters.viewHeight=h;parameters.renderWidth=rw;parameters.renderHeight=rh;
  parameters.logitOffset=UINT(offset/4);enabled=true;
 }
 // No depth-direction guess: FFX context flags are not carried by this addon.
 // The opt-in requires an explicit convention alongside the unjittered-MV contract.
 static int DepthDirection(){
  const wchar_t*v=_wgetenv(L"DLSS5_FAST_HISTORY_DEPTH_INVERTED");
  return v&&!wcscmp(v,L"0")?0:v&&!wcscmp(v,L"1")?1:-1;
 }
 bool Prepare(NativeGameSubmission&submit,ID3D12Resource*motion,ID3D12Resource*depth,
              const NativeTemporalFrameMetadata&m,bool reset){
  current=nullptr;
  if(!enabled)return false;
  const int inverted=DepthDirection();
  if(!motion||!depth||!m.Valid()||inverted<0){prior=false;seed=0;return false;}
  auto md=motion->GetDesc(),dd=depth->GetDesc();
  if(NativeFastHistorySupport::TextureIssue(md)||NativeFastHistorySupport::TextureIssue(dd)||
     NativeFastHistorySupport::MotionFormat(md.Format)==DXGI_FORMAT_UNKNOWN||
     NativeFastHistorySupport::DepthFormat(dd.Format)==DXGI_FORMAT_UNKNOWN||
     !md.Width||!md.Height||dd.Width<parameters.renderWidth||dd.Height<parameters.renderHeight){prior=false;seed=0;return false;}
  auto&s=slots[next++%slots.size()];auto done=submit.Completed();
  if(done==UINT64_MAX)throw std::runtime_error("fast history device removed");
  if(s.fence>done)submit.Flush(); // bounded ring pressure only, not a per-frame wait
  s.motion=motion;s.depth=depth;s.heap=NativeFastHistory::History::Binding(submit.Device(),motion,depth);
  if(!s.control){
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
   D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;rd.Height=1;
   rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   NativeFastHistorySupport::Check(submit.Device()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&s.control)),"frame control");
  }
  reset=reset||!prior||m.frame_id!=frame+1||m.pre_exposure!=exposure;
  if(reset)seed=0;
  parameters.useHistory=!reset;parameters.motionWidth=UINT(md.Width);parameters.motionHeight=md.Height;
  parameters.scaleX=m.motion_scale[0]/parameters.renderWidth;parameters.scaleY=m.motion_scale[1]/parameters.renderHeight;
  parameters.depthInverted=UINT(inverted); // unjittered vectors; no jitter delta added
  void*p{};D3D12_RANGE none{};NativeFastHistorySupport::Check(s.control->Map(0,&none,&p),"frame control map");
  std::memcpy(p,&parameters,sizeof parameters);s.control->Unmap(0,nullptr);
  frame=m.frame_id;exposure=m.pre_exposure;current=&s;return parameters.useHistory!=0;
 }
 UINT Seed()const{return seed;}
 void Inputs(ID3D12GraphicsCommandList*c,ID3D12Resource*raw,ID3D12Resource*out,D3D12_RESOURCE_STATES ds){
  if(current)history.RecordInputs(c,raw,out,current->motion.Get(),current->depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,ds,parameters,current->heap.Get(),current->control->GetGPUVirtualAddress());
 }
 void Outputs(ID3D12GraphicsCommandList*c,ID3D12Resource*raw,ID3D12Resource*out,D3D12_RESOURCE_STATES ds){
  if(current)history.RecordOutputs(c,raw,out,current->depth.Get(),ds,parameters,current->heap.Get(),current->control->GetGPUVirtualAddress());
 }
 void Submitted(UINT64 fence){if(current){current->fence=fence;prior=true;++seed;}else{prior=false;seed=0;}}
};
