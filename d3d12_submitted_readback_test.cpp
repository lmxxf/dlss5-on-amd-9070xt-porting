#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_submitted_readback.h"
int wmain(){try{
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 ID3D12Resource*texture=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
 std::vector<unsigned short>expected(1920*1080*4);
 for(size_t i=0;i<expected.size();i++)expected[i]=static_cast<unsigned short>((i*37)%0x3c01);
 auto*upload=buf(d,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};ck(upload->Map(0,&none,&p));for(UINT y=0;y<1080;y++)std::memcpy(static_cast<char*>(p)+fp.Offset+y*fp.Footprint.RowPitch,expected.data()+size_t(y)*1920*4,1920*8);upload->Unmap(0,nullptr);
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ID3D12CommandAllocator*a=nullptr;ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));ID3D12GraphicsCommandList*c=nullptr;ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a,nullptr,IID_PPV_ARGS(&c)));
 D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};c->ResourceBarrier(1,&b);ck(c->Close());ID3D12CommandList*lists[]={c};q->ExecuteCommandLists(1,lists);
 // No CPU wait for producer: the readback must establish ordering/completion.
 for(UINT i=0;i<2;i++){auto out=NativeReadSubmittedFrame(q,texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);if(out.size()!=expected.size()*2||std::memcmp(out.data(),expected.data(),out.size()))throw std::runtime_error("readback mismatch");printf("submitted_readback pass=%u bytes=%zu exact=1\n",i,out.size());}
 return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
