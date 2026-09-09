#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_game_submission.h"
int wmain(int argc,wchar_t**argv){try{
 if(argc!=2)return 2;IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("AMD missing");
 constexpr UINT64 bytes=65536*16;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
 ID3D12Resource*out=nullptr;ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));auto*rb=buf(d,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 D3D12_ROOT_PARAMETER p{};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p.Descriptor.ShaderRegister=0;D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&p;ID3DBlob*b=nullptr,*e=nullptr;ck(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e));ID3D12RootSignature*root=nullptr;ck(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)));b->Release();if(e)e->Release();b=e=nullptr;
 auto path=std::wstring(argv[1])+L"\\native_fp8_quantize_check.hlsl";auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e);if(e){if(FAILED(hr))fwrite(e->GetBufferPointer(),1,e->GetBufferSize(),stderr);e->Release();}ck(hr);D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={b->GetBufferPointer(),b->GetBufferSize()};ID3D12PipelineState*pso=nullptr;ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));b->Release();
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 submit.Submit([&](ID3D12GraphicsCommandList*c){c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootUnorderedAccessView(0,out->GetGPUVirtualAddress());c->Dispatch(1024,1,1);D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={out,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&t);c->CopyBufferRegion(rb,0,out,0,bytes);});
 UINT*data=nullptr;D3D12_RANGE range{0,bytes},none{};ck(rb->Map(0,&range,reinterpret_cast<void**>(&data)));size_t finite=0,different=0;
 for(UINT i=0;i<65536;i++){if(data[i*4+2]!=i)throw std::runtime_error("output coverage");if(data[i*4+3]){finite++;if(data[i*4]!=data[i*4+1]){if(different<8)printf("half=%04x old=%08x new=%08x\n",i,data[i*4],data[i*4+1]);different++;}}}rb->Unmap(0,&none);
 printf("finite_half=%zu bit_different=%zu\n",finite,different);return finite==63488&&!different?0:1;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
