#include "fast_history_fixture.h"
#include "../src/native_fast_history.h"
#include <cstring>

// Shader-only fixture: Gpu::Run waits for completion before the next write.
// This does not test the product's asynchronous control/recording scheduler.
struct SynchronousControl {
 ComPtr<ID3D12Resource> upload;
 void Create(ID3D12Device* d){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"control buffer");}
 void Write(const void* data,size_t size){Require(size<=256,"control size");void* p=nullptr;D3D12_RANGE empty{};Check(upload->Map(0,&empty,&p),"control map");memcpy(p,data,size);upload->Unmap(0,nullptr);}
 D3D12_GPU_VIRTUAL_ADDRESS Address()const{return upload->GetGPUVirtualAddress();}
};
// Independent double-precision five-tap reference. Deliberately samples a
// nonlinear field; an ordinary bilinear warp cannot pass the fractional test.
static double Reference5(const std::vector<float>& rgb,unsigned w,unsigned h,double x,double y)
{
    struct Axis { double p0,pm,p2,a,b,c; };
    const auto axis=[](double x) {double base=std::floor(x-.5)+.5,t=x-base;
        double a=-.5*t*(1-t)*(1-t),c=-.5*t*t*(1-t),b=1-a-c;
        double w2=.5*t+2*t*t-1.5*t*t*t;
        return Axis{base-1,base+w2/b,base+2,a,b,c};};
    const auto sample=[&](double px,double py){px=std::clamp(px-.5,0.,double(w-1));py=std::clamp(py-.5,0.,double(h-1));
        unsigned x0=unsigned(px),y0=unsigned(py),x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
        double fx=px-x0,fy=py-y0;
        return (1-fy)*((1-fx)*rgb[(y0*w+x0)*3]+fx*rgb[(y0*w+x1)*3])+fy*((1-fx)*rgb[(y1*w+x0)*3]+fx*rgb[(y1*w+x1)*3]);};
    auto a=axis(x),b=axis(y);double weights[]={a.a*b.b,b.a*a.b,a.b*b.b,b.c*a.b,a.c*b.b};
    double values[]={sample(a.p0,b.pm),sample(a.pm,b.p0),sample(a.pm,b.pm),sample(a.pm,b.p2),sample(a.p2,b.pm)};
    double sum=0,total=0;for(int i=0;i<5;++i){sum+=weights[i]*values[i];total+=weights[i];}return sum/total;
}
// Exercise the real pre/post shaders over every pixel, including reflected rows.
// The large surfaces exceeded the old one-dimensional dispatch limit. The narrow
// case catches missing per-row bounds checks in a partially filled thread group.
static void CheckDispatchCoverage(Gpu& g,unsigned w,unsigned h,unsigned ph,bool direct=false)
{
    const unsigned n=w*ph;
    auto motion=g.Texture(1,1,DXGI_FORMAT_R32G32_FLOAT),depth=g.Texture(1,1,DXGI_FORMAT_R32_FLOAT);
    g.Upload(motion.Get(),{0,0});g.Upload(depth.Get(),{.5f});
    auto raw=g.Buffer(UINT64(n)*16),output=g.Buffer(UINT64(n)*20);
    {
        std::vector<float> colour(size_t(n)*4,.4f);
        g.Upload(raw.Get(),colour);
    }
    const auto expected=[&](unsigned i,unsigned c){
        unsigned x=i%w,y=i/w;if(y>=h)y=2*h-y-2;
        // A smooth field isolates dispatch coverage from subpixel roundoff at
        // discontinuities; every row, column and channel still has its own value.
        return .25f+float(x)/(8*w)+float(y)/(8*h)+float(c)/64;
    };
    const auto uploadNetwork=[&](bool prime){
        std::vector<float> network(size_t(n)*5,0);
        for(unsigned i=0;i<n;++i)for(unsigned c=0;c<3;++c)network[i*3+c]=prime?expected(i,c):.8f;
        g.Upload(output.Get(),network);
    };
    uploadNetwork(true);
    auto shared=g.Buffer(UINT64(n)*16);
    g.Run([&](auto*cmd){NativeFastHistorySupport::Transition(cmd,shared.Get(),ReadState,D3D12_RESOURCE_STATE_COMMON);});
    NativeFastHistory::History hist;hist.Create(g.device.Get(),w,h,ph,direct?shared.Get():nullptr);
    auto binding=hist.Binding(g.device.Get(),motion.Get(),depth.Get());
    const auto readWarp=[&](ID3D12Resource* r){
        if(r==shared.Get())g.Run([&](auto*cmd){NativeFastHistorySupport::Transition(cmd,r,D3D12_RESOURCE_STATE_COMMON,ReadState);});
        auto result=g.Read(r);
        if(r==shared.Get())g.Run([&](auto*cmd){NativeFastHistorySupport::Transition(cmd,r,ReadState,D3D12_RESOURCE_STATE_COMMON);});
        return result;
    };
    SynchronousControl control;control.Create(g.device.Get());

    NativeFastHistory::Parameters p{};p.width=w;p.height=h;p.processingHeight=ph;p.viewWidth=w;p.viewHeight=h;
    p.renderWidth=p.renderHeight=p.motionWidth=p.motionHeight=1;p.hasDepth=0;p.logitOffset=n*3;
    const auto inputs=[&]{control.Write(&p,sizeof(p));
        g.Run([&](auto*cmd){hist.RecordInputs(cmd,raw.Get(),output.Get(),motion.Get(),depth.Get(),ReadState,ReadState,p,binding.Get(),control.Address());});};
    const auto finish=[&]{control.Write(&p,sizeof(p));
        g.Run([&](auto*cmd){hist.RecordOutputs(cmd,raw.Get(),output.Get(),depth.Get(),ReadState,p,binding.Get(),control.Address());});};
    inputs();
    for(auto* warp:{hist.Warped(),hist.PostWarped()}) {
        const auto pixels=readWarp(warp);
        for(unsigned i=0;i<n;++i) {
            for(unsigned c=0;c<3;++c)Require(pixels[i*4+c]==.4f,"prime dispatch must write every raw pixel");
            Require(pixels[i*4+3]==0,"prime history invalid across entire surface");
        }
    }
    finish();p.useHistory=1;inputs();
    for(auto* warp:{hist.Warped(),hist.PostWarped()}) {
        const auto pixels=readWarp(warp);
        for(unsigned i=0;i<n;++i) {
            for(unsigned c=0;c<3;++c)if(!(std::abs(pixels[i*4+c]-expected(i,c))<2e-6f)) {
                std::fprintf(stderr,"surface=%ux%u pixel=(%u,%u) channel=%u actual=%.9g expected=%.9g\n",w,ph,i%w,i/w,c,pixels[i*4+c],expected(i,c));
                Require(false,"history reprojects every pixel including padded rows");
            }
            Require(pixels[i*4+3]==1,"history valid across entire surface");
        }
    }
    uploadNetwork(false);finish();
    const float weight=.73974609375f*.5f;
    {
        const auto pixels=g.Read(output.Get());
        for(unsigned i=0;i<n;++i)for(unsigned c=0;c<3;++c)
            Require(std::abs(pixels[i*3+c]-(.8f+weight*(expected(i,c)-.8f)))<2e-6f,"post dispatch blends every output pixel");
        for(size_t i=size_t(n)*3;i<pixels.size();++i)Require(pixels[i]==0,"post dispatch preserves logits");
    }
    g.NoErrors();std::printf("native temporal dispatch coverage PASS %ux%u processing=%ux%u direct=%d\n",w,h,w,ph,int(direct));
}
int main(int argc,char** argv)try
{
    Require(argc==1||(argc==2&&std::strcmp(argv[1],"--hardware")==0),"usage: lmxxf_native_temporal [--hardware]");
    Gpu g(argc==2);constexpr unsigned w=8,h=8,n=w*h;
    auto motion=g.Texture(w,h,DXGI_FORMAT_R32G32_FLOAT),depth=g.Texture(w,h,DXGI_FORMAT_R32_FLOAT);
    auto raw=g.Buffer(n*16),output=g.Buffer(n*20);
    std::vector<float> colour(n*4,.4f),network(n*5,0),rgb(n*3),vectors(n*2,0),depths(n,.5f);
    for(unsigned i=0;i<n;++i){colour[i*4+3]=1;for(unsigned c=0;c<3;++c)rgb[i*3+c]=.3f+.002f*(i%w)*(i%w)+.02f*std::sin(float(i));}
    std::copy(rgb.begin(),rgb.end(),network.begin());
    g.Upload(raw.Get(),colour);g.Upload(output.Get(),network);g.Upload(motion.Get(),vectors);g.Upload(depth.Get(),depths);
    NativeFastHistory::History hist;hist.Create(g.device.Get(),w,h,h);auto binding=hist.Binding(g.device.Get(),motion.Get(),depth.Get());
    SynchronousControl control;control.Create(g.device.Get());

    NativeFastHistory::Parameters p{};p.width=w;p.height=h;p.processingHeight=h;p.viewWidth=w;p.viewHeight=h;
    p.renderWidth=w;p.renderHeight=h;p.motionWidth=w;p.motionHeight=h;p.scaleX=1.f/w;p.scaleY=1.f/h;p.logitOffset=n*3;
    const auto inputs=[&]{control.Write(&p,sizeof(p));g.Run([&](auto*cmd){hist.RecordInputs(cmd,raw.Get(),output.Get(),motion.Get(),depth.Get(),ReadState,ReadState,p,binding.Get(),control.Address());});};
    const auto finish=[&]{control.Write(&p,sizeof(p));g.Run([&](auto*cmd){hist.RecordOutputs(cmd,raw.Get(),output.Get(),depth.Get(),ReadState,p,binding.Get(),control.Address());});};
    const auto reset=[&]{p.useHistory=0;p.jitterX=p.jitterY=0;g.Upload(output.Get(),network);inputs();finish();p.useHistory=1;};
    inputs();auto pre=g.Read(hist.Warped());Require(pre[3]==0,"first frame history gated");finish();
    Require(g.Read(output.Get())==network,"first frame RGB and logit buffer unchanged");
    p.useHistory=1;inputs();pre=g.Read(hist.Warped());auto post=g.Read(hist.PostWarped());
    for(unsigned i=0;i<n;++i)Require(pre[i*4+3]==1&&std::abs(pre[i*4]-rgb[i*3])<2e-6f&&std::abs(post[i*4]-rgb[i*3])<2e-6f,"pre/post identity");
    p.jitterX=.35f/w;p.jitterY=.65f/h;inputs();pre=g.Read(hist.Warped());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        Require(std::abs(pre[(y*w+x)*4]-Reference5(rgb,w,h,x+.85,y+1.15))<3e-6,"fractional five-tap reference and tap clamp");
    // The two native paths sample motion differently at a silhouette.
    p.jitterX=p.jitterY=0;depths[4*w+4]=.1f;vectors[(4*w+4)*2]=1;
    g.Upload(depth.Get(),depths);g.Upload(motion.Get(),vectors);inputs();pre=g.Read(hist.Warped());post=g.Read(hist.PostWarped());
    const unsigned target=3*w+3;
    Require(std::abs(pre[target*4]-rgb[(target+1)*3])<2e-6,"pre selects nearest-depth diagonal motion");
    Require(std::abs(post[target*4]-rgb[target*3])<2e-6,"post keeps centre motion");
    depths[4*w+4]=.9f;p.depthInverted=1;g.Upload(depth.Get(),depths);inputs();pre=g.Read(hist.Warped());
    Require(std::abs(pre[target*4]-rgb[(target+1)*3])<2e-6,"reversed depth selection");
    p.hasDepth=0;inputs();pre=g.Read(hist.Warped());Require(std::abs(pre[target*4]-rgb[target*3])<2e-6,"optional depth disabled");p.hasDepth=1;
    // Integration must retain the existing disocclusion guards even though the
    // native model itself can assign a large weight to an incompatible sample.
    depths[target]=.95f;g.Upload(depth.Get(),depths);inputs();post=g.Read(hist.PostWarped());
    Require(post[target*4+3]==0,"newly revealed depth rejects post history");
    depths[target]=.5f;g.Upload(depth.Get(),depths);
    for(unsigned c=0;c<3;++c)colour[target*4+c]=.7f;
    g.Upload(raw.Get(),colour);inputs();post=g.Read(hist.PostWarped());
    Require(post[target*4+3]==0,"changed raw colour rejects post history");
    for(unsigned c=0;c<3;++c)colour[target*4+c]=.4f;
    g.Upload(raw.Get(),colour);
    // Offscreen history uses per-tap clamp, matching the model's addressing.
    std::fill(vectors.begin(),vectors.end(),0);vectors[target*2]=-100;g.Upload(motion.Get(),vectors);p.hasDepth=0;inputs();post=g.Read(hist.PostWarped());
    Require(post[target*4+3]==1&&std::abs(post[target*4]-rgb[(3*w)*3])<2e-6,"offscreen per-tap clamp");
    vectors[target*2]=std::numeric_limits<float>::quiet_NaN();g.Upload(motion.Get(),vectors);inputs();post=g.Read(hist.PostWarped());
    Require(post[target*4+3]==0,"nonfinite motion rejected");
    std::fill(vectors.begin(),vectors.end(),0);g.Upload(motion.Get(),vectors);inputs();
    // Predictable model logit: sigmoid(0) * exact stored model blend scale.
    std::fill(network.begin(),network.begin()+n*3,.8f);g.Upload(output.Get(),network);finish();auto mixed=g.Read(output.Get());
    const float weight=.73974609375f*.5f;
    for(unsigned i=0;i<n;++i)Require(std::abs(mixed[i*3]-(.8f+weight*(rgb[i*3]-.8f)))<2e-6,"native post blend");
    inputs();post=g.Read(hist.PostWarped());for(unsigned i=0;i<n;++i)Require(std::abs(post[i*4]-mixed[i*3])<2e-6,"feedback stores model blended output");
    p.historyStrength=0;g.Upload(output.Get(),network);finish();Require(g.Read(output.Get())==network,"zero history strength bit identity");p.historyStrength=1;
    inputs();std::fill(network.begin(),network.begin()+n*3,0);g.Upload(output.Get(),network);finish();Require(g.Read(output.Get())==network,"failed zero neural output cannot resurrect history");
    inputs();post=g.Read(hist.PostWarped());Require(post[target*4+3]==0,"failed output not valid next history");
    // A reset bypasses old model pixels even with a stale nonzero logit buffer.
    std::fill(network.begin(),network.begin()+n*3,.6f);reset();inputs();post=g.Read(hist.PostWarped());Require(std::abs(post[target*4]-.6f)<2e-6,"reset primes fresh model");
    // A fitted viewport: boundaries must not read letterbox pixels.
    p.viewX=2;p.viewWidth=4;reset();inputs();pre=g.Read(hist.Warped());Require(pre[3]==0&&pre[2*4+3]==1&&pre[6*4+3]==0,"fit viewport validity");
    CheckDispatchCoverage(g,65,3,4);
    CheckDispatchCoverage(g,65,3,4,true);
    CheckDispatchCoverage(g,3456,1440,1472,true); // Free-resolution 3440x1440 input.
    CheckDispatchCoverage(g,3840,2160,2176,true);
    g.NoErrors();std::printf("native temporal %s PASS: distinct pre/post motion, 5tap, edges, reversed depth, post model feedback, zero recovery/reset/fit, dispatch coverage\n",argc==2?"GPU":"WARP");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
