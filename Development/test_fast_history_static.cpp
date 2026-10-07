#include <cstring>
#include "fast_history_fixture.h"
#include "../src/native_fast_history.h"
int main(int argc,char**)try{
 Gpu g(argc>1);constexpr unsigned w=8,h=8,ph=8,n=w*ph;
 auto raw=g.Buffer(n*16),out=g.Buffer(n*20);
 NativeFastHistory::History hist;hist.Create(g.device.Get(),w,h,ph);
 auto heap=hist.Binding(g.device.Get(),nullptr,nullptr);
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
 D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ComPtr<ID3D12Resource>cb;Check(g.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&cb)),"control");
 NativeFastHistory::Parameters p{};p.width=w;p.height=h;p.processingHeight=ph;p.viewWidth=w;p.viewHeight=h;p.renderWidth=w;p.renderHeight=h;p.logitOffset=n*3;p.hasDepth=0;p.reserved[0]=0;
 auto control=[&]{void*ptr{};D3D12_RANGE range{};Check(cb->Map(0,&range,&ptr),"map");memcpy(ptr,&p,sizeof p);cb->Unmap(0,nullptr);};
 auto inputs=[&]{control();g.Run([&](auto*c){hist.RecordInputs(c,raw.Get(),out.Get(),nullptr,nullptr,ReadState,ReadState,p,heap.Get(),cb->GetGPUVirtualAddress());});};
 auto finish=[&]{control();g.Run([&](auto*c){hist.RecordOutputs(c,raw.Get(),out.Get(),nullptr,ReadState,p,heap.Get(),cb->GetGPUVirtualAddress());});};
 std::vector<float>r(n*4,.4f),v(n*5,0);std::fill(v.begin(),v.begin()+n*3,.2f);g.Upload(raw.Get(),r);g.Upload(out.Get(),v);inputs();finish();Require(g.Read(out.Get())==v,"cold identity");
 p.useHistory=1;std::fill(v.begin(),v.begin()+n*3,.4f);g.Upload(out.Get(),v);inputs();auto pre=g.Read(hist.Warped());for(unsigned i=0;i<n;i++)Require(pre[i*4+3]==1,"static prior valid");finish();auto mix=g.Read(out.Get());const float expected=.4f+.369873046875f*(.2f-.4f);for(unsigned i=0;i<n*3;i++)Require(std::isfinite(mix[i])&&std::abs(mix[i]-expected)<2e-6f,"learned static feedback nonzero");
 std::fill(r.begin(),r.end(),.7f);g.Upload(raw.Get(),r);g.Upload(out.Get(),v);inputs();finish();Require(g.Read(out.Get())==v,"scene jump rejects old model");
 p.useHistory=0;inputs();finish();Require(g.Read(out.Get())==v,"reset identity");g.NoErrors();printf("static mode1 PASS null-guides cold/nonzero-history/raw-jump/reset\n");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
