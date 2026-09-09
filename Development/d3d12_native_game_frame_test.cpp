#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_game_frame.h"
// Full integration boundary test, with mandatory independently generated output.
// No success based only on nonblack/finite pixels. No in-game acceptance claim.
int wmain(int argc,wchar_t**argv){try{
 if(argc!=4)return 2;std::wstring dir=argv[1],fixture=argv[3];
 auto read=[](const std::wstring&p,size_t size){std::ifstream f(p.c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(size))throw std::runtime_error("frame fixture missing/size mismatch");std::vector<unsigned char>v(size);f.seekg(0);if(!f.read(reinterpret_cast<char*>(v.data()),size))throw std::runtime_error("frame fixture truncated");return v;};
 constexpr size_t bytes=1920ull*1080*8;
 auto pixels=read(fixture+L"\\source.f16",bytes),expected=read(fixture+L"\\expected.f16",bytes);
 auto raw_noise=read(argv[2],201326592);std::vector<float>noise(raw_noise.size()/4);std::memcpy(noise.data(),raw_noise.data(),raw_noise.size());
 // Reject NaN/Inf before heavy network initialization.
 for(const auto*v:{&pixels,&expected})for(size_t i=0;i<v->size();i+=2){uint16_t h;std::memcpy(&h,v->data()+i,2);if((h&0x7c00)==0x7c00)throw std::runtime_error("nonfinite frame fixture");}
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ID3D12Resource*source=nullptr,*target=nullptr;
 ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)));
 ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&target)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size;d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&size);if(size!=bytes||fp.Offset||fp.Footprint.RowPitch!=1920*8)throw std::runtime_error("unexpected texture footprint");
 auto*upload=buf(d,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);auto*rb=buf(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);void*p=nullptr;D3D12_RANGE none{};ck(upload->Map(0,&none,&p));std::memcpy(p,pixels.data(),bytes);upload->Unmap(0,nullptr);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;dst.pResource=source;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);});
 auto*frame=new NativeGameFrame; // Retain on failure/timeout; GPU may still own it.
 frame->Create(q,source,noise,dir);
 for(UINT i=0;i<3;i++){
  frame->ProcessSubmittedFrame(target,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST,0,false);
  submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=target;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  D3D12_RANGE range{0,bytes};ck(rb->Map(0,&range,&p));size_t diff=0;auto*a=static_cast<unsigned char*>(p);for(size_t k=0;k<bytes;k+=2)diff+=a[k]!=expected[k]||a[k+1]!=expected[k+1];
  std::ofstream out((fixture+L"\\actual-frame.f16").c_str(),std::ios::binary);if(!out.write(static_cast<char*>(p),bytes))throw std::runtime_error("save frame failed");rb->Unmap(0,&none);
  printf("game_frame frame=%u half_values=8294400 different=%zu history=0\n",i,diff);fflush(stdout);if(diff)throw std::runtime_error("frame differs from independent reference");
 }
 delete frame;puts("full_color_frame=exact; fixed first-frame fixture, not game acceptance");return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
