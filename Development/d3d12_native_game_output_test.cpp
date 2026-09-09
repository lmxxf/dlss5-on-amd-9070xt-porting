#define wmain input_test_unused_wmain
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_game_rgb_output.h"
#include "native_game_submission.h"

int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 info{};a->GetDesc1(&info);if(info.VendorId==0x1002){ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();break;}a->Release();}if(!d)throw std::runtime_error("AMD missing");
 auto*input=buf(d,1920ull*1152*3*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
 D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R10G10B10A2_UNORM;td.SampleDesc.Count=1;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ID3D12Resource*texture=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_PPV_ARGS(&texture)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);auto*rb=buf(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 NativeGameRgbOutput output;output.Create(d,input,argv[1]);ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);q->Release();
 // Integer/2048 values give exactly representable products with 1023, including
 // clamping and ties. Padding has a distinct sentinel and must never be copied.
 auto numerator=[](UINT x,UINT y,UINT ch,bool b){return int((x*17+y*11+ch*613+(b?719:0))%4096)-1024;};
 for(UINT frame=0;frame<5;frame++){
  bool alternate=frame==2;void*p=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&p));auto*v=static_cast<float*>(p);
  for(UINT y=0;y<1152;y++)for(UINT x=0;x<1920;x++)for(UINT ch=0;ch<3;ch++)v[(size_t(y)*1920+x)*3+ch]=y<1080?numerator(x,y,ch,alternate)/2048.f:17.f;input->Unmap(0,nullptr);
  submit.Submit([&](ID3D12GraphicsCommandList*c){output.Record(c,texture,D3D12_RESOURCE_STATE_COPY_SOURCE);});
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=texture;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);});
  D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&p));size_t different=0;
  for(UINT y=0;y<1080;y++){auto*row=reinterpret_cast<const UINT*>(static_cast<const unsigned char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);for(UINT x=0;x<1920;x++){UINT expected=3u<<30;for(UINT ch=0;ch<3;ch++){int n=std::max(0,std::min(2048,numerator(x,y,ch,alternate)));UINT code=UINT(n*1023+1024)/2048;expected|=code<<(ch*10);}different+=row[x]!=expected;}}
  rb->Unmap(0,&none);std::printf("frame=%u input=%c packed_pixels=2073600 different=%zu\n",frame,alternate?'B':'A',different);std::fflush(stdout);if(different)throw std::runtime_error("display texture mismatch");
 }
 std::puts("display_texture=exact R10 A/A/B/A/A owned_submit=10; game color space/present unverified");return 0;
}catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
