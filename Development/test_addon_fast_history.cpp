#include "fast_history_fixture.h"
#include "../src/native_addon_fast_history.h"
#include "../src/native_game_oneshot.h" // compile the actual HIP addon call chain
int main(int argc,char**){try{
 _wputenv_s(L"DLSS5_FAST_HISTORY_DEPTH_INVERTED",L"1");
 Gpu g(argc>1);NativeGameSubmission submit;submit.Create(g.queue.Get(),true,nullptr,true);
 constexpr UINT w=8,h=8,n=w*h;
 auto motion=g.Texture(w,h,DXGI_FORMAT_R32G32_FLOAT),depth=g.Texture(w,h,DXGI_FORMAT_R32_FLOAT);
 g.Upload(motion.Get(),std::vector<float>(n*2,0));g.Upload(depth.Get(),std::vector<float>(n,.5f));
 auto raw=g.Buffer(n*16),output=g.Buffer(n*20),shared=g.Buffer(n*16);
 g.Upload(raw.Get(),std::vector<float>(n*4,.4f));
 g.Run([&](auto*c){NativeFastHistorySupport::Transition(c,shared.Get(),ReadState,D3D12_RESOURCE_STATE_COMMON);});
 NativeAddonFastHistory history;history.Create(g.device.Get(),w,h,h,w,h,n*12,shared.Get());
 NativeTemporalFrameMetadata m{};m.ffx_pre=true;m.motion_scale[0]=w;m.motion_scale[1]=h;
 Require(!history.Prepare(submit,nullptr,depth.Get(),m,false),"missing motion resets history");
 ComPtr<ID3D12Fence> gate;Check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"gate");
 Check(g.queue->Wait(gate.Get(),1),"queue pause");
 std::vector<ComPtr<ID3D12Resource>> uploads,readbacks;
 std::vector<float> expected;
 float previous=0;
 // Queue all controls while GPU is paused: a mutable shared CB/heap cannot pass.
 for(UINT i=0;i<10;++i){
  m.frame_id=i;bool reset=i==0||i==4;
  bool use=history.Prepare(submit,motion.Get(),depth.Get(),m,reset);
  Require(use==!reset,"reset and continuous frame policy");
  Require(history.Seed()==(i<4?i:i-4),"seed reset policy");
  auto upload=g.Buffer(n*20,D3D12_HEAP_TYPE_UPLOAD),rb=g.Buffer(n*20,D3D12_HEAP_TYPE_READBACK);
  std::vector<float> network(n*5,0);float value=.2f+.02f*i;std::fill(network.begin(),network.begin()+n*3,value);
  void*p{};D3D12_RANGE none{};Check(upload->Map(0,&none,&p),"upload map");memcpy(p,network.data(),n*20);upload->Unmap(0,nullptr);
  uploads.push_back(upload);readbacks.push_back(rb);
  submit.Submit([&](auto*c){history.Inputs(c,raw.Get(),output.Get(),ReadState);
   NativeFastHistorySupport::Transition(c,output.Get(),ReadState,D3D12_RESOURCE_STATE_COPY_DEST);
   c->CopyBufferRegion(output.Get(),0,upload.Get(),0,n*20);
   NativeFastHistorySupport::Transition(c,output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,ReadState);
   history.Outputs(c,raw.Get(),output.Get(),ReadState);
   NativeFastHistorySupport::Transition(c,output.Get(),ReadState,D3D12_RESOURCE_STATE_COPY_SOURCE);
   c->CopyBufferRegion(rb.Get(),0,output.Get(),0,n*20);
   NativeFastHistorySupport::Transition(c,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,ReadState);
  });history.Submitted(submit.LastValue());
  previous=use?value+.369873046875f*(previous-value):value;expected.push_back(previous);
 }
 Check(gate->Signal(1),"queue release");submit.Flush();
 for(UINT i=0;i<10;++i){void*p{};D3D12_RANGE range{0,n*20},none{};Check(readbacks[i]->Map(0,&range,&p),"readback");
  for(UINT j=0;j<n*3;++j)Require(std::abs(static_cast<float*>(p)[j]-expected[i])<2e-6,"immutable deferred history controls and feedback");readbacks[i]->Unmap(0,&none);}
 m.frame_id=11;Require(!history.Prepare(submit,motion.Get(),depth.Get(),m,false),"frame gap resets history");
 m.frame_id=12;m.pre_exposure=2;Require(!history.Prepare(submit,motion.Get(),depth.Get(),m,false),"exposure change resets history");
 g.NoErrors();puts("addon fast history deferred controls, reset/gap/exposure, guide ownership PASS");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
