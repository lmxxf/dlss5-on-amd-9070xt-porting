#pragma once
#include "native_pso.h"
#include "native_pinned_resource.h"
#include <cmath>
#include "native_resident_table.h"
#include "native_preblock_runtime.h"
#include "native_shader_cache.h"
#include "native_network_timestamps.h"
#include "native_matrix_workspace.h"
class NativeSplit {
 NativeMatrixWorkspace*workspace{};ID3D12Resource*qkv_weights{};ID3D12PipelineState*aux_pso[2]{};ID3D12PipelineState*direct_pso[3]{};ID3D12Resource*qkv_weights8{};bool direct_attention{};bool matrix_attention{},wave_ffwd{},parallel_ffwd{};
 ID3D12Resource*input{};ID3D12Resource*weights[3]{};ID3D12Resource*result[4]{};ID3D12Resource*project_weights[2]{};bool wave_project{},copy8{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[4]{};UINT geometry[2]{};bool recorded{},tiled_projection{},shared_ffwd{},split_tiled{},ffwd_tiled{},stream8{},in8{},out8{};UINT geometry6[6]{};
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("C64 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource* Buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;rd.Height=1;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;Check(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&rd,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));if(data){void*p=nullptr;D3D12_RANGE empty{};Check(r->Map(0,&empty,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);}
public:
 NativeSplit()=default;NativeSplit(const NativeSplit&)=delete;
 ~NativeSplit(){for(auto*p:direct_pso)if(p)p->Release();if(qkv_weights8)qkv_weights8->Release();if(qkv_weights)qkv_weights->Release();for(auto*r:project_weights)if(r)r->Release();for(auto*p:aux_pso)if(p)p->Release();if(input)input->Release();for(auto*r:weights)if(r)r->Release();for(auto*r:result)if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 /* map (FAST PATH DLSS5_SPLIT_STREAM8): {raster_width,raster_height,pad_x,pad_y} of the window this block works on; the kernels read the block input by
    raster index (border tokens zero) and write the cropped raster directly, so NativeSplitWindow runs no pack/crop dispatch. stream bit 1 = the input raster is
    E4M3 bytes (previous block's output), bit 2 = this block's output raster is E4M3 bytes; the first-projection output is kept as E4M3 tiles and doubles as the
    QKV input (no pack dispatch). All values were already on the E4M3 grid (F(H()) outputs), so the chain is bit for bit the same. */
 void Create(ID3D12Device*d,ID3D12Resource*src,UINT width,UINT height,const std::vector<float>&fw,const std::vector<float>&fp,const std::vector<float>&aw,const std::wstring&dir,bool raw_output=false,NativeMatrixWorkspace*shared=nullptr,const UINT*map=nullptr,UINT stream=0){
  if(input||!d||!src||!width||!height||width%8||height%8||fw.size()!=524288||fp.size()!=262656||aw.size()!=1114640)throw std::runtime_error("split contract");
  input=src;input->AddRef();geometry[0]=width;geometry[1]=height;stream8=map!=nullptr;in8=stream8&&(stream&1u);out8=stream8&&(stream&2u);
  geometry6[0]=width;geometry6[1]=height;if(stream8){if(raw_output&&out8)throw std::runtime_error("split stream: raw output is f32");geometry6[2]=map[0];geometry6[3]=map[1];geometry6[4]=map[2];geometry6[5]=map[3];}
  const wchar_t*tile=_wgetenv(L"DLSS5_TEST_SPLIT_PROJECTION");if(tile&&wcscmp(tile,L"0")&&wcscmp(tile,L"1"))throw std::runtime_error("invalid split projection flag");tiled_projection=tile&&!wcscmp(tile,L"1");
  if(tiled_projection&&UINT64(width)*height/8>65535)throw std::runtime_error("split tiled extent");
  const wchar_t*share=_wgetenv(L"DLSS5_TEST_SPLIT_FFWD");if(share&&wcscmp(share,L"0")&&wcscmp(share,L"1"))throw std::runtime_error("invalid shared FFWD flag");shared_ffwd=share&&!wcscmp(share,L"1");if(shared_ffwd&&UINT64(width)*height>65535)throw std::runtime_error("shared FFWD extent");
  weights[0]=Buffer(d,fw.size()*4,&fw);weights[1]=Buffer(d,fp.size()*4,&fp);weights[2]=Buffer(d,aw.size()*4,&aw);
  for(auto&r:result)r=Buffer(d,UINT64(width)*height*512*4);
  if(stream8){for(UINT i=0;i<3;i++){result[i]->Release();result[i]=Buffer(d,UINT64(width)*height*512);}result[3]->Release();result[3]=Buffer(d,UINT64(geometry6[2])*geometry6[3]*512*(out8?1:4));}
 NativeInitTick("split: enter"); const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_SPLIT_FFWD");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave split FFWD flag");wave_ffwd=wave_flag&&!wcscmp(wave_flag,L"1");
  const wchar_t*parallel_flag=_wgetenv(L"DLSS5_TEST_PARALLEL_SPLIT_FFWD");if(parallel_flag&&wcscmp(parallel_flag,L"0")&&wcscmp(parallel_flag,L"1"))throw std::runtime_error("invalid parallel split FFWD flag");parallel_ffwd=parallel_flag&&!wcscmp(parallel_flag,L"1");wave_ffwd=wave_ffwd||parallel_ffwd;
  if(wave_ffwd){
   if(UINT64(width)*height/16>65535)throw std::runtime_error("wave split FFWD extent");
   std::vector<float>packed(fw.size()/2);for(size_t i=0;i<fw.size();i++){uint32_t bits;std::memcpy(&bits,&fw[i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("split FFWD weights not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   /* FAST PATH (DLSS5_SPLIT_FFWD_TILED): mix [512][512], expand [8][256][64], contract [8][64][256] f16 as 1KB [k 32][j 16] tiles (kernel built with NATIVE_SPLIT_FFWD_TILED=1). */
   {const wchar_t*ft=_wgetenv(L"DLSS5_SPLIT_FFWD_TILED");if(ft&&wcscmp(ft,L"0")&&wcscmp(ft,L"1"))throw std::runtime_error("invalid split ffwd tiled flag");ffwd_tiled=ft&&!wcscmp(ft,L"1");if(ffwd_tiled&&!parallel_ffwd)throw std::runtime_error("split ffwd tiled needs the parallel FFWD kernel");}
   if(ffwd_tiled){uint16_t*h16=reinterpret_cast<uint16_t*>(packed.data());auto tile=[&](size_t base,size_t n_rows,size_t k_cols){std::vector<uint16_t>t(n_rows*k_cols);for(size_t n=0;n<n_rows;n++)for(size_t k=0;k<k_cols;k++)t[((n/16)*(k_cols/32)+k/32)*512+(k%32)*16+n%16]=h16[base+n*k_cols+k];std::memcpy(h16+base,t.data(),t.size()*2);};
    tile(0,512,512);for(size_t g=0;g<8;g++){tile(262144+g*16384,256,64);tile(393216+g*16384,64,256);}}
NativeInitTick("split: ffwd pack cpu");   auto*u=Buffer(d,packed.size()*4,&packed);auto*local=NativeResidentTable(d,u);u->Release();weights[0]->Release();weights[0]=local;
  }
NativeInitTick("split: ffwd upload+resident");  D3D12_ROOT_PARAMETER params[6]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,6};params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[5].Descriptor.ShaderRegister=1;D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=6;desc.pParameters=params;ID3DBlob*blob=nullptr,*error=nullptr;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
  const wchar_t*matrix_flag=_wgetenv(L"DLSS5_TEST_MATRIX_SPLIT_ATTENTION");if(matrix_flag&&wcscmp(matrix_flag,L"0")&&wcscmp(matrix_flag,L"1"))throw std::runtime_error("invalid split matrix attention flag");
  matrix_attention=matrix_flag&&!wcscmp(matrix_flag,L"1");
  if(matrix_attention){
   if(!shared)throw std::runtime_error("split matrix attention requires graph workspace");shared->Validate(d,UINT64(width)*height*512);workspace=shared;
   std::vector<float>packed(3*512*512/2);
   for(UINT block=0;block<48;block++)for(UINT g=0;g<16;g++)for(UINT row=0;row<32;row++)for(UINT j=0;j<32;j++){
    float v=aw[(block*32+row)*512+g*32+j];uint32_t bits;std::memcpy(&bits,&v,4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m){int e=int(m>>23)-112;if(e<=0||e>=31||(m&0x1fff))throw std::runtime_error("split QKV weight not exact half");h|=uint16_t((e<<10)|((m&0x7fffff)>>13));}
    size_t dst=(((block*16+g)*32+row)*32+j)*2;std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+dst,&h,2);
   }
   auto*upload=Buffer(d,packed.size()*4,&packed);qkv_weights=NativeResidentTable(d,upload);upload->Release();
   const wchar_t*names[]={L"native_matrix_pack_c512.cso",L"native_matrix_qkv_c512.cso"};
   for(UINT i=0;i<2;i++){blob=nullptr;Check(D3DReadFileToBlob((dir+L"\\"+names[i]).c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&aux_pso[i]));blob->Release();Check(hr);}
  }
  // FAST PATH: direct attention path for C512 (FP8 pack -> fused QKV+normalize -> direct attention with FP8 Q/K/V and FP8 output -> projection with direct FP8 A loads).
  if(const wchar_t*da=_wgetenv(L"DLSS5_SPLIT_DIRECT_ATTENTION")){if(wcscmp(da,L"0")&&wcscmp(da,L"1"))throw std::runtime_error("invalid split direct attention flag");const wchar_t*f8=_wgetenv(L"DLSS5_FP8_OPERANDS");direct_attention=!wcscmp(da,L"1")&&matrix_attention&&f8&&!wcscmp(f8,L"1");}
  /* FAST PATH (DLSS5_SPLIT_TILED): C512 QKV and projection weights as contiguous 512-byte [k 32][j 16] tiles (kernels built with NATIVE_TILED_WEIGHTS=1); no 512-byte row strides. */
  {const wchar_t*st=_wgetenv(L"DLSS5_SPLIT_TILED");if(st&&wcscmp(st,L"0")&&wcscmp(st,L"1"))throw std::runtime_error("invalid split tiled flag");split_tiled=st&&!wcscmp(st,L"1");}
  auto tile512=[](unsigned char*o8,size_t rows){std::vector<unsigned char>t(rows*512);for(size_t n=0;n<rows/16;n++)for(size_t g=0;g<16;g++)for(size_t kk=0;kk<32;kk++)for(size_t j=0;j<16;j++)t[(n*16+g)*512+kk*16+j]=o8[(n*16+j)*512+g*32+kk];std::memcpy(o8,t.data(),t.size());};
  if(direct_attention){
   std::vector<float>packed8(3ull*512*512/4);unsigned char*out8=reinterpret_cast<unsigned char*>(packed8.data());
   for(size_t i=0;i<3ull*512*512;i++){float v=aw[i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){out8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("split QKV weight not FP8-representable");out8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("split QKV weight not FP8-representable");out8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}
NativeInitTick("split: qkv fp8 pack cpu");   if(split_tiled)tile512(out8,3*512);
   auto*upload=Buffer(d,packed8.size()*4,&packed8);qkv_weights8=NativeResidentTable(d,upload);upload->Release();
NativeInitTick("split: qkv upload+resident");   const wchar_t*names[]={L"native_matrix_pack_fp8_c512.cso",stream8?L"native_wave_qkv_normalize_fp8qkv_stream_c512.cso":L"native_wave_qkv_normalize_fp8qkv_c512.cso",L"native_wave_attention_direct_fp8qkv_fp8act_c512.cso"};
   for(UINT i=0;i<3;i++){ID3DBlob*blob=nullptr;Check(D3DReadFileToBlob((dir+L"\\"+names[i]).c_str(),&blob));D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};auto hr=NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&direct_pso[i]));blob->Release();Check(hr);}
  }
NativeInitTick("split: qkv pso");  // Wave-matrix FFWD/attention projections: [0]=fp (matrix 0, scale 262144), [1]=aw (matrix 3*MATRIX, scale 4*MATRIX+16*4096+16).
  if(const wchar_t*wp=_wgetenv(L"DLSS5_TEST_WAVE_SPLIT_PROJECT")){if(wcscmp(wp,L"0")&&wcscmp(wp,L"1"))throw std::runtime_error("invalid wave split project flag");wave_project=!wcscmp(wp,L"1");}
  // FAST PATH (DLSS5_SPLIT_COPY8): the stage-1 projection also writes the E4M3 copy the attention path reads; the pack dispatch is skipped.
  {const wchar_t*c8=_wgetenv(L"DLSS5_SPLIT_COPY8");if(c8&&wcscmp(c8,L"0")&&wcscmp(c8,L"1"))throw std::runtime_error("invalid split copy8 flag");copy8=wave_project&&c8&&!wcscmp(c8,L"1");}
  if(stream8&&!(direct_attention&&wave_project&&parallel_ffwd&&!copy8))throw std::runtime_error("split stream needs direct attention, wave projections and the parallel FFWD (no copy8)");
  if(wave_project){
   const size_t matrix=512ull*512;const float*sources[2]={fp.data(),aw.data()+3*matrix};const float*scales[2]={fp.data()+matrix,aw.data()+4*matrix+16*4096+16};
   for(UINT k=0;k<2;k++){
    const wchar_t*f8=_wgetenv(L"DLSS5_FP8_OPERANDS");const bool fp8=f8&&!wcscmp(f8,L"1");
    std::vector<float>packed(fp8?matrix/4+512:matrix/2+512);
    if(fp8){unsigned char*o8=reinterpret_cast<unsigned char*>(packed.data());for(size_t i=0;i<matrix;i++){float v=sources[k][i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("split projection weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("split projection weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}std::memcpy(packed.data()+matrix/4,scales[k],512*4);if(split_tiled)tile512(o8,512);}
    else{for(size_t i=0;i<matrix;i++){uint32_t bits;std::memcpy(&bits,sources[k]+i,4);uint32_t mag=bits&0x7fffffffu;uint16_t half=uint16_t((bits>>16)&0x8000);if(mag){int e=int(mag>>23)-112;if(e<=0||e>=31||(mag&0x1fff))throw std::runtime_error("split projection weight not exact half");half|=uint16_t((e<<10)|((mag&0x7fffff)>>13));}std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&half,2);}
    std::memcpy(packed.data()+matrix/2,scales[k],512*4);}
    project_weights[k]=Buffer(d,packed.size()*4,&packed);
    if(k==0){weights[1]->Release();weights[1]=Buffer(d,16);} // the f32 ffwd-projection copy is only read by the non-wave path
   }
  }
NativeInitTick("split: project packs+upload");  const wchar_t*pad=_wgetenv(L"DLSS5_TEST_PAD_MULTIHEAD_LDS");if(pad&&wcscmp(pad,L"0")&&wcscmp(pad,L"1"))throw std::runtime_error("invalid multihead LDS flag");
  const char*entry[]={shared_ffwd?"ffwd_shared":"ffwd",tiled_projection?"tiled_split_project":"ffwd_projection","attention",tiled_projection?"tiled_attention_project":"projection"};D3D_SHADER_MACRO macros[]={{"RAW_OUTPUT","0"},{"CHANNELS","512"},{"NATIVE_PAD_MULTIHEAD_LDS",pad&&!wcscmp(pad,L"1")?"1":"0"},{nullptr,nullptr}};
  for(UINT i=0;i<4;i++){macros[0].Definition=raw_output&&i==3?"1":"0";auto path=dir+((i==0||(i==1&&!tiled_projection))?L"\\native_split.hlsl":L"\\native_c64.hlsl");blob=nullptr;error=nullptr;HRESULT hr=stream8&&i!=2?D3DReadFileToBlob((dir+(i==0?(in8?L"\\native_wave_split_ffwd_parallel_stream8.cso":L"\\native_wave_split_ffwd_parallel_streamf.cso"):i==1?(in8?L"\\native_wave_project_stream08_c512.cso":L"\\native_wave_project_stream0f_c512.cso"):(out8?L"\\native_wave_project_stream18_c512.cso":raw_output?L"\\native_wave_project_stream1raw_c512.cso":L"\\native_wave_project_stream1f_c512.cso"))).c_str(),&blob):((i==1||i==3)&&wave_project)?D3DReadFileToBlob((dir+((i==3&&raw_output)?(direct_attention?L"\\native_wave_project_raw_fp8act_c512.cso":L"\\native_wave_project_raw_c512.cso"):(i==3&&direct_attention)?L"\\native_wave_project_fp8act_c512.cso":(i==1&&copy8)?L"\\native_wave_project_copy8_c512.cso":L"\\native_wave_project_c512.cso")).c_str(),&blob):(i==0&&wave_ffwd)?D3DReadFileToBlob((dir+(parallel_ffwd?L"\\native_wave_split_ffwd_parallel.cso":L"\\native_wave_split_ffwd.cso")).c_str(),&blob):(i==2&&matrix_attention)?D3DReadFileToBlob((dir+L"\\native_wave_split_attention.cso").c_str(),&blob):CompileNativeShader(path,macros,entry[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"split shader failed";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};Check(NativeCreateComputePipelineState(d,&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}

 NativeInitTick("split: end");
 }
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr){
  if(recorded)for(auto*r:result)Barrier(c,r,true);
  for(UINT i=0;i<4;i++){
   if(i==2&&direct_attention){
    if(workspace->packed_readable&&!copy8)Barrier(c,workspace->packed,true);if(workspace->norm_readable)Barrier(c,workspace->norm,true);
    if(!copy8&&!stream8){c->SetComputeRootSignature(root);c->SetPipelineState(direct_pso[0]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,6,geometry6,0);
    {UINT n=geometry[0]*geometry[1]*2;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);}Barrier(c,workspace->packed,false);workspace->packed_readable=true;}else c->SetComputeRootSignature(root);if(timer)timer->Mark(c,"split_qkv_pack");
    c->SetPipelineState(direct_pso[1]);c->SetComputeRootShaderResourceView(0,(stream8?result[1]:workspace->packed)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,qkv_weights8->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,weights[2]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->norm->GetGPUVirtualAddress());c->Dispatch(geometry[0]*geometry[1]/16,24,1);Barrier(c,workspace->norm,false);workspace->norm_readable=true;if(timer)timer->Mark(c,"split_qkv_matrix");
    c->SetPipelineState(direct_pso[2]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,weights[2]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,workspace->norm->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[2]->GetGPUVirtualAddress());c->Dispatch(geometry[0]*geometry[1]/64,16,1);Barrier(c,result[2],false);if(timer)timer->Mark(c,"split_stage2");
    continue;
   }
   if(i==2&&matrix_attention){
    if(workspace->packed_readable)Barrier(c,workspace->packed,true);if(workspace->qkv_readable)Barrier(c,workspace->qkv,true);
    c->SetComputeRootSignature(root);c->SetPipelineState(aux_pso[0]);c->SetComputeRootShaderResourceView(0,result[1]->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->packed->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,6,geometry6,0);
    UINT n=geometry[0]*geometry[1]*4;c->Dispatch(std::min(n,65535u),(n+65534)/65535,1);Barrier(c,workspace->packed,false);workspace->packed_readable=true;
    if(timer)timer->Mark(c,"split_qkv_pack");
    c->SetPipelineState(aux_pso[1]);c->SetComputeRootShaderResourceView(0,workspace->packed->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,qkv_weights->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,workspace->qkv->GetGPUVirtualAddress());c->Dispatch((geometry[0]*geometry[1]+31)/32,16,3);Barrier(c,workspace->qkv,false);workspace->qkv_readable=true;
    if(timer)timer->Mark(c,"split_qkv_matrix");
   }
   c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,(i?result[i-1]:input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,(((i==1||i==3)&&wave_project)?project_weights[i==1?0:1]:weights[i<2?i:2])->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,(i==3?result[1]:(i==2&&matrix_attention?workspace->qkv:input))->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,result[i]->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,6,geometry6,0);if(i==1&&copy8){if(workspace->packed_readable)Barrier(c,workspace->packed,true);c->SetComputeRootUnorderedAccessView(5,workspace->packed->GetGPUVirtualAddress());}if((i==1||i==3)&&wave_project)c->Dispatch(geometry[0]*geometry[1]/16,8,1);else if(i==0&&wave_ffwd)c->Dispatch(geometry[0]*geometry[1]/16,parallel_ffwd?8:1,1);else if(i==0&&shared_ffwd)c->Dispatch(geometry[0]*geometry[1],1,1);else if(tiled_projection&&(i==1||i==3))c->Dispatch(16,geometry[0]*geometry[1]/8,1);else c->Dispatch(geometry[0]*geometry[1]/64,i==2?16:1,1);Barrier(c,result[i],false);if(i==1&&copy8){Barrier(c,workspace->packed,false);workspace->packed_readable=true;}if(timer)timer->Mark(c,"split_stage"+std::to_string(i));}recorded=true;
 }
 ID3D12Resource* Stage(UINT i)const{if(i>=4)throw std::runtime_error("split stage index");return result[i];}
 ID3D12Resource* Output()const{return result[3];}ID3D12Resource* Input()const{return input;}
};
