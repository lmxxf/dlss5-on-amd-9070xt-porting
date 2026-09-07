#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_rgb_texture.h"
#include "native_game_submission.h"
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 const size_t N=1920ull*1152*3;auto*input=buf(d,N*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
 // Upload GENERIC_READ includes NON_PIXEL_SHADER_RESOURCE; no input transition.
 auto*bridge=new NativeRgbTexture;bridge->Create(d,input,argv[1]);
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;auto td=bridge->Output()->GetDesc();d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
 auto*rb=buf(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 auto half_value=[](unsigned h){return h<1024?std::ldexp(float(h),-24):std::ldexp(float(1024+(h&1023)),int(h>>10)-25);};
 std::vector<unsigned short>expected(1920ull*1080*3);
 for(UINT frame=0;frame<3;frame++){
  void*p=nullptr;D3D12_RANGE none{};ck(input->Map(0,&none,&p));auto*v=static_cast<float*>(p);
  for(size_t i=0;i<N;i++){
   unsigned h=1024+unsigned((i+(frame==1?997:0))%(0x3bff-1024));
   float low=half_value(h),high=half_value(h+1),middle=(low+high)*.5f;
   unsigned mode=unsigned(i%4);float value=mode==0?low:mode==1?middle:std::nextafter(middle,mode==2?low:high);
   v[i]=i<expected.size()?value:123.f;
   // Original CUDA post HALF surface is truncation for nonnegative RGB;
   // independently established by check_native_post_half_surface.py.
   if(i<expected.size())expected[i]=h;
  }input->Unmap(0,nullptr);
  submit.Submit([&](ID3D12GraphicsCommandList*c){bridge->Record(c);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={bridge->Output(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=bridge->Output();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});
  D3D12_RANGE range{0,SIZE_T(bytes)};ck(rb->Map(0,&range,&p));size_t different=0;
  for(UINT y=0;y<1080;y++){auto*row=reinterpret_cast<unsigned short*>(static_cast<char*>(p)+fp.Offset+y*fp.Footprint.RowPitch);for(UINT x=0;x<1920;x++){for(UINT ch=0;ch<3;ch++){size_t k=(size_t(y)*1920+x)*3+ch;bool bad=row[x*4+ch]!=expected[k];if(bad&&different<12)printf("mismatch k=%zu mode=%zu got=%04x expected=%04x\n",k,k%4,row[x*4+ch],expected[k]);different+=bad;}different+=row[x*4+3]!=0x3c00;}}
  rb->Unmap(0,&none);printf("rgb_texture frame=%u values=8294400 different=%zu nonnegative_truncate=1\n",frame,different);fflush(stdout);if(different)throw std::runtime_error("RGB texture mismatch");
 }delete bridge;return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
