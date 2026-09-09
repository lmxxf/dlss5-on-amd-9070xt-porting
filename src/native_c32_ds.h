#pragma once
#include "native_resident_table.h"
#include "native_preblock_runtime.h"
#include "native_matrix_workspace.h"
// Consumes the raw-before-quantization half-pool produced by NativeC32Stage.
class NativeC32Downsample {
 NativeMatrixWorkspace*workspace{};ID3D12PipelineState*pool_pso{};bool wave_head{};UINT wave_channels{};
 // FAST PATH (DLSS5_WAVE_C32_DS): wave-matrix C32 pooled projection with an E4M3 weight copy (native_wave_c32_ds.cso).
 bool wave_c32{};
 ID3D12Resource *input{},*weights{},*output{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso{};
 UINT geometry[5]{};bool recorded{};
 static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("C32 DS HRESULT="+std::to_string(unsigned(h)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,bool upload){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=upload?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=upload?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,upload?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 void Barrier(ID3D12GraphicsCommandList*c,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeC32Downsample()=default;NativeC32Downsample(const NativeC32Downsample&)=delete;
 ~NativeC32Downsample(){if(pool_pso)pool_pso->Release();if(input)input->Release();if(weights)weights->Release();if(output)output->Release();if(root)root->Release();if(pso)pso->Release();}
 // Raw path pools already-cropped half-valued HWC, then projects C -> 2C.
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,UINT shift,const std::vector<float>&w,const std::wstring&dir,bool c64_raw=false,UINT channels=64,NativeMatrixWorkspace*shared=nullptr){
  const bool padded_head=c64_raw&&channels==512&&width==60&&height==36;
  if(channels==512&&!padded_head&&(width<8||height<8||width%8||height%8))throw std::runtime_error("unverified split pool/head extent");
  if(input||!d||!src||(channels!=64&&channels!=128&&channels!=256&&channels!=512)||(!c64_raw&&channels!=64)||w.size()!=(c64_raw?2*channels*channels:2048)||!width||!height||(c64_raw?(width%2||height%2):(width%8||height%8))||shift>3||(c64_raw&&shift))throw std::runtime_error("DS contract");
  input=src;input->AddRef();geometry[0]=width/2;geometry[1]=height/2;geometry[3]=(shift&1)?2:0;geometry[4]=(shift&2)?2:0;geometry[2]=width/2+geometry[3]*2;
  if(c64_raw)geometry[2]=width;
  if(padded_head){geometry[0]=32;geometry[1]=20;geometry[3]=width/2;geometry[4]=height/2;}
  output=Buffer(d,UINT64(geometry[0])*geometry[1]*(c64_raw?2*channels:64)*4,false);weights=Buffer(d,w.size()*4,true);void*ptr=nullptr;D3D12_RANGE empty{};Check(weights->Map(0,&empty,&ptr));std::memcpy(ptr,w.data(),w.size()*4);weights->Unmap(0,nullptr);weights=NativeMaybeResident(d,weights);
  {const wchar_t*wc=_wgetenv(L"DLSS5_WAVE_C32_DS");if(wc&&wcscmp(wc,L"0")&&wcscmp(wc,L"1"))throw std::runtime_error("invalid wave C32 DS flag");wave_c32=!c64_raw&&wc&&!wcscmp(wc,L"1");}
  if(wave_c32){
   if(geometry[0]%16)throw std::runtime_error("wave C32 DS needs width % 16 == 0");
   std::vector<float>packed(512);unsigned char*o8=reinterpret_cast<unsigned char*>(packed.data());
   for(size_t i=0;i<2048;i++){float v=w[i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("C32 DS weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("C32 DS weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}
   auto*u=Buffer(d,packed.size()*4,true);Check(u->Map(0,&empty,&ptr));std::memcpy(ptr,packed.data(),packed.size()*4);u->Unmap(0,nullptr);auto*local=NativeResidentTable(d,u);u->Release();weights->Release();weights=local;
  }
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_WAVE_HEAD");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid wave head flag");wave_head=padded_head&&flag&&!wcscmp(flag,L"1");
  const wchar_t*ds_flag=_wgetenv(L"DLSS5_TEST_WAVE_DOWNSAMPLE");if(ds_flag&&wcscmp(ds_flag,L"0")&&wcscmp(ds_flag,L"1"))throw std::runtime_error("invalid wave downsample flag");wave_head=wave_head||(c64_raw&&ds_flag&&!wcscmp(ds_flag,L"1"));wave_channels=channels;
  auto wave_file=[&](const wchar_t*stem){return dir+L"\\"+stem+(channels==512?L"":L"_c"+std::to_wstring(channels))+L".cso";};
  if(wave_head){
   const UINT64 p=UINT64(geometry[0])*geometry[1];if(p%16||p/16>65535||p*channels/128>65535)throw std::runtime_error("wave downsample extent");
   if(!shared)throw std::runtime_error("wave head requires workspace");shared->Validate(d,UINT64(geometry[0])*geometry[1]*channels);workspace=shared;
   std::vector<float>packed(w.size()/2);for(size_t i=0;i<w.size();i++){uint32_t bits;std::memcpy(&bits,&w[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("head weights not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   auto*u=Buffer(d,packed.size()*4,true);Check(u->Map(0,&empty,&ptr));std::memcpy(ptr,packed.data(),packed.size()*4);u->Unmap(0,nullptr);auto*local=NativeResidentTable(d,u);u->Release();weights->Release();weights=local;
  }
  D3D12_ROOT_PARAMETER parameters[4]{};parameters[0].ParameterType=parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[1].Descriptor.ShaderRegister=1;parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[3].Constants={0,0,5};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=parameters;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();error=nullptr;
  if(wave_head){blob=nullptr;Check(D3DReadFileToBlob(wave_file(L"native_head_pool").c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pool_pso));blob->Release();Check(hr);}
  auto channel_text=std::to_string(channels);D3D_SHADER_MACRO macros[]={{"CHANNELS",channel_text.c_str()},{nullptr,nullptr}};
  auto path=dir+(c64_raw?L"\\native_c64_ds.hlsl":L"\\native_c32_ds.hlsl");auto hr=wave_head?D3DReadFileToBlob(wave_file(L"native_wave_head_project").c_str(),&blob):wave_c32?D3DReadFileToBlob((dir+L"\\native_wave_c32_ds.cso").c_str(),&blob):D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error);
  if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"DS shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));blob->Release();
 }
 void Record(ID3D12GraphicsCommandList*c){if(recorded)Barrier(c,true);
 if(wave_head){
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={workspace->packed,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};if(workspace->packed_readable)c->ResourceBarrier(1,&b);
  c->SetComputeRootSignature(root);c->SetPipelineState(pool_pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,workspace->packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,5,geometry,0);c->Dispatch(geometry[0]*geometry[1]*wave_channels/128,1,1);
  std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);workspace->packed_readable=true;
  c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,workspace->packed->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());c->Dispatch(geometry[0]*geometry[1]/16,2*wave_channels/16,1);Barrier(c,false);recorded=true;return;
 }
 c->SetComputeRootSignature(root);c->SetPipelineState(pso);c->SetComputeRootShaderResourceView(0,input->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,5,geometry,0);if(wave_c32)c->Dispatch(geometry[0]*geometry[1]/16,1,1);else c->Dispatch((geometry[0]*geometry[1]+63)/64,1,1);Barrier(c,false);recorded=true;}
 ID3D12Resource* Output()const{return output;}
};
