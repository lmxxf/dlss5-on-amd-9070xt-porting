#define wmain unused_rgb_main
#include "d3d12_native_game_rgb_test.cpp"
#undef wmain
#include "native_game_submission.h"
#include "native_game_codec.h"
// Execute captured and candidate DXBC on identical half input, compare half output.
int wmain(int argc,wchar_t**argv){try{
 if(argc!=4&&argc!=5)return 2;
 const bool decode=argc==5&&(!wcscmp(argv[4],L"decode")||!wcscmp(argv[4],L"decode-game"));
 const bool game=argc==5&&(!wcscmp(argv[4],L"game")||!wcscmp(argv[4],L"decode-game"));if(argc==5&&!game&&!decode)return 2;
 const UINT inputs=decode?3:1;
 const wchar_t*stage_dir=_wgetenv(L"DLSS5_TEST_CODEC_STAGE_DIR");
 if(stage_dir&&!game)throw std::runtime_error("codec stage requires game geometry");
 const wchar_t*fixture_dir=_wgetenv(L"DLSS5_CODEC_FIXTURE_DIR");
 if(fixture_dir&&(!game||stage_dir))throw std::runtime_error("fixture export requires original game test");
 UINT vendor=wcstoul(argv[3],nullptr,16);
 IDXGIFactory6*f=nullptr;ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&f)));ID3D12Device*d=nullptr;
 for(UINT i=0;;i++){IDXGIAdapter1*a=nullptr;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==vendor)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}if(!d)throw std::runtime_error("adapter missing");
 auto read=[](const wchar_t*p){std::ifstream in(p,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("DXBC missing");auto n=in.tellg();if(n<=0||n>1024*1024)throw std::runtime_error("DXBC size");std::vector<char>v(static_cast<size_t>(n));in.seekg(0);if(!in.read(v.data(),n))throw std::runtime_error("DXBC truncated");return v;};
 auto original=read(argv[1]),candidate=read(argv[2]);
 const UINT W=game?1920:256,H=game?1080:256;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=W;td.Height=H;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.SampleDesc.Count=1;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ID3D12Resource*input[3]{},*out[2]{};
 for(UINT j=0;j<inputs;j++)ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&input[j])));
 td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;for(auto&v:out)ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&v)));
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;d->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&bytes);
 auto*upload=buf(d,bytes*inputs,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);auto*rb=buf(d,bytes*2,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 auto sample=[&](UINT pixel,UINT ch,UINT texture){
  unsigned short v=static_cast<unsigned short>((pixel+ch*7919)&65535);
  if(decode){v=static_cast<unsigned short>((pixel*37+ch*7919+texture*3571)%0x3c01);if(texture==2&&ch<3)v=static_cast<unsigned short>((pixel*13+ch*113)%0x4c01);if(texture==1&&pixel%17==0)v=0;}
  if((v&0x7c00)==0x7c00)v=0;return v;
 };
 void*m=nullptr;D3D12_RANGE none{};ck(upload->Map(0,&none,&m));
 for(UINT j=0;j<inputs;j++)for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++)for(UINT ch=0;ch<4;ch++){auto v=sample(y*W+x,ch,j);std::memcpy(static_cast<char*>(m)+bytes*j+fp.Offset+y*fp.Footprint.RowPitch+x*8+ch*2,&v,2);}upload->Unmap(0,nullptr);
 std::vector<uint16_t>fixture_alpha;
 if(fixture_dir)for(UINT j=0;j<inputs;j++){
  const wchar_t*name=decode?(j==0?L"\\oracle-encode.f16":j==1?L"\\oracle-neural.f16":L"\\source.f16"):L"\\source.f16";
  std::wstring path=std::wstring(fixture_dir)+name;
  std::ifstream f(path.c_str(),std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(bytes))throw std::runtime_error("source fixture size");
  std::vector<unsigned char>raw(bytes);f.seekg(0);if(!f.read(reinterpret_cast<char*>(raw.data()),bytes))throw std::runtime_error("source fixture truncated");
  for(size_t k=0;k<bytes;k+=2){uint16_t h;std::memcpy(&h,raw.data()+k,2);if((h&0x7c00)==0x7c00)throw std::runtime_error("nonfinite fixture input");}
  if(decode&&j==2){fixture_alpha.resize(bytes/8);for(size_t k=0;k<fixture_alpha.size();k++)std::memcpy(&fixture_alpha[k],raw.data()+k*8+6,2);}
  ck(upload->Map(0,&none,&m));std::memcpy(static_cast<char*>(m)+bytes*j,raw.data(),bytes);upload->Unmap(0,nullptr);
 }
 ID3D12DescriptorHeap*heap=nullptr;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,inputs+2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ck(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
 D3D12_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=td.Format;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;for(UINT j=0;j<inputs;j++){if(j)cpu.ptr+=stride;d->CreateShaderResourceView(input[j],&sv,cpu);}
 D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=td.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;for(auto*v:out){cpu.ptr+=stride;d->CreateUnorderedAccessView(v,nullptr,&uv,cpu);}
 D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,inputs,decode?1u:0u,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0}};D3D12_ROOT_PARAMETER p[3]{};for(UINT i=0;i<2;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[i].DescriptorTable={1,&ranges[i]};}p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[2].Constants={0,0,16};
 D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=3;rd.pParameters=p;ID3DBlob*b=nullptr,*err=nullptr;ck(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&b,&err));ID3D12RootSignature*root=nullptr;ck(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)));b->Release();if(err)err->Release();
 auto*stage=stage_dir?new NativeGameCodec:nullptr;
 if(stage)stage->Create(d,std::vector<ID3D12Resource*>(input,input+inputs),stage_dir);
 ID3D12PipelineState*pso[2]{};for(UINT i=0;i<2;i++){auto&code=i?candidate:original;D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={code.data(),code.size()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));}
 ID3D12CommandQueue*q=nullptr;D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));NativeGameSubmission submit;submit.Create(q);
 auto transition=[](ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&v);};
 submit.Submit([&](ID3D12GraphicsCommandList*c){for(UINT j=0;j<inputs;j++){D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;src.PlacedFootprint.Offset+=bytes*j;dst.pResource=input[j];dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(c,input[j],D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}});
 for(UINT test=0;test<3;test++){
  uint32_t words[16]={W,H,W,H,0,0,W,H,0x3f800000,0x3f800000,0x3f800000,1};float scale=test==0?1.f:test==1?.5f:2.f;std::memcpy(words+8,&scale,4);
  submit.Submit([&](ID3D12GraphicsCommandList*c){c->SetDescriptorHeaps(1,&heap);c->SetComputeRootSignature(root);auto gpu=heap->GetGPUDescriptorHandleForHeapStart();c->SetComputeRootDescriptorTable(0,gpu);c->SetComputeRoot32BitConstants(2,16,words,0);
   for(UINT i=0;i<2;i++){if(i==1&&stage){
    stage->Record(c,std::vector<D3D12_RESOURCE_STATES>(inputs,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),scale);
    transition(c,stage->Output(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=stage->Output();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;dst.PlacedFootprint.Offset+=bytes;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    transition(c,stage->Output(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);continue;
   }if(test)transition(c,out[i],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);auto u=gpu;u.ptr+=(i+inputs)*stride;c->SetComputeRootDescriptorTable(1,u);c->SetPipelineState(pso[i]);c->Dispatch((W+15)/16,(H+15)/16,1);transition(c,out[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=out[i];src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;dst.PlacedFootprint.Offset+=bytes*i;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
  });
  D3D12_RANGE range{0,SIZE_T(bytes*2)};ck(rb->Map(0,&range,&m));size_t different=0,invalid=0;auto*a=static_cast<unsigned short*>(m);for(size_t i=0;i<bytes/2;i++){different+=a[i]!=a[bytes/2+i];for(UINT side=0;side<2;side++){auto v=a[side*bytes/2+i];invalid+=(i%4==3)?v!=(decode?(fixture_dir?fixture_alpha[i/4]:sample(UINT(i/4),3,2)):0x3c00):((v&0x7c00)==0x7c00||(!decode&&v>0x3c00));}}if(fixture_dir&&test==0&&!different&&!invalid){std::ofstream file((std::wstring(fixture_dir)+(decode?L"\\expected.f16":L"\\oracle-encode.f16")).c_str(),std::ios::binary);if(!file.write(static_cast<char*>(m),bytes))throw std::runtime_error("encode export failed");}rb->Unmap(0,&none);printf("codec vendor=%x size=%ux%u scale=%g half_values=%llu different=%zu invalid=%zu\n",vendor,W,H,scale,(unsigned long long)bytes/2,different,invalid);fflush(stdout);if(different||invalid)throw std::runtime_error("codec differs or invalid coverage");
 }if(stage)delete stage;return 0;
}catch(const std::exception&e){fprintf(stderr,"%s\n",e.what());return 1;}}
