#pragma once
#include "native_pinned_resource.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include "native_resident_table.h"
#include "native_network_timestamps.h"

// Input: tile-major 8x8 RGBA32F. Outputs: full HWC32 and half-resolution HWC32,
// represented as FP32 values on the E4M3 lattice. No CPU readback in Record.
class NativePreblockRuntime {
 ID3D12Device* device{};
 ID3D12Resource*prefix_ffn_weights{},*test_prefix_readback{};bool prefix_wave{};
 ID3D12Resource *ffn{},*raw{},*main{},*down{},*weights[2]{},*noise{},*temporal{};
 ID3D12RootSignature *root{},*finish_root{};
 ID3D12PipelineState* pso[4]{};
 ID3D12DescriptorHeap* heap[4]{};
 UINT width{},height{};UINT64 buffer_bytes{};bool coalesced_finish{},recorded{},shared_raw{},wave_ffn{},wave_ffn_local{};ID3D12Resource*attention_local{},*main8{};ID3D12RootSignature*split_root{};ID3D12PipelineState*split_pso[4]{};bool split_attention{},fused_attention{},wave_attention{};bool blocked_ffn_raw_store{};ID3D12RootSignature*ffn_root{};ID3D12Resource*stage_input{},*mapped_source{};bool shared_c32{};UINT mapping[8]{};
 static ID3D12Resource*&SharedFfn(){static ID3D12Resource*r=nullptr;return r;}
 static ID3D12Resource*&SharedRaw(){static ID3D12Resource*r=nullptr;return r;}
 static UINT64&SharedBytes(){static UINT64 b=0;return b;}
 static bool&SharedFfnReadable(){static bool v=false;return v;}
 static bool&SharedRawReadable(){static bool v=false;return v;}
 static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("native preblock HRESULT="+std::to_string(unsigned(hr)));}
 ID3D12Resource* Buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
  D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=h.VisibleNodeMask=1;
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=type==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
  ID3D12Resource*r=nullptr;Check(NativeCreateCommittedResource(device,&h,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;
 }
 ID3D12RootSignature* Root(UINT srvs,UINT uavs,bool with_noise=false,bool with_temporal=false){
  D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,srvs,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,uavs,0,0,srvs}};
  D3D12_ROOT_PARAMETER p[4]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,13};p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[2].Descriptor={2,0};p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[3].Descriptor={3,0};
  D3D12_ROOT_SIGNATURE_DESC d{};d.NumParameters=with_temporal?4:with_noise?3:2;d.pParameters=p;ID3DBlob*b=nullptr,*error=nullptr;
  Check(D3D12SerializeRootSignature(&d,D3D_ROOT_SIGNATURE_VERSION_1,&b,&error));ID3D12RootSignature*r=nullptr;Check(device->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&r)));b->Release();if(error)error->Release();return r;
 }
 void Heap(UINT stage,ID3D12Resource*a,UINT64 asize,ID3D12Resource*b,UINT64 bsize,ID3D12Resource*c,UINT64 csize,bool finish){
  D3D12_DESCRIPTOR_HEAP_DESC d{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&heap[stage])));
  auto h=heap[stage]->GetCPUDescriptorHandleForHeapStart();UINT step=device->GetDescriptorHandleIncrementSize(d.Type);
  ID3D12Resource* resources[]={a,b,c};UINT64 sizes[]={asize,bsize,csize};
  for(UINT i=0;i<3;i++){
   if(i<(finish?1u:2u)){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Buffer.StructureByteStride=4;s.Buffer.NumElements=UINT(sizes[i]/4);if(i==0&&((stage==0&&wave_ffn_local)||(stage==3&&prefix_wave)||(stage==1&&attention_local))){s.Format=DXGI_FORMAT_R32_TYPELESS;s.Buffer.StructureByteStride=0;s.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;}device->CreateShaderResourceView(resources[i],&s,h);}
   else{D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.StructureByteStride=4;u.Buffer.NumElements=UINT(sizes[i]/4);device->CreateUnorderedAccessView(resources[i],nullptr,&u,h);}
   h.ptr+=step;
  }
 }
 static void Barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&x);}
public:
 ~NativePreblockRuntime(){if(test_prefix_readback)test_prefix_readback->Release();if(attention_local)attention_local->Release();if(main8)main8->Release();if(split_root)split_root->Release();for(auto*p:split_pso)if(p)p->Release();if(ffn_root)ffn_root->Release();if(stage_input)stage_input->Release();if(prefix_ffn_weights)prefix_ffn_weights->Release();for(auto*r:{ffn,raw,main,down,weights[0],weights[1],noise,temporal})if(r)r->Release();for(auto*p:pso)if(p)p->Release();for(auto*h:heap)if(h)h->Release();if(root)root->Release();if(finish_root)finish_root->Release();}
 NativePreblockRuntime()=default;NativePreblockRuntime(const NativePreblockRuntime&)=delete;NativePreblockRuntime& operator=(const NativePreblockRuntime&)=delete;
 void Create(ID3D12Device*d,ID3D12Resource*input,UINT w,UINT h,const std::vector<float>&fw,const std::vector<float>&aw,const std::wstring&shader_dir,bool live_profile,bool raw_features=false,const std::vector<float>*noise_table=nullptr,ID3D12Resource*temporal_input=nullptr){
  if(noise_table&&(raw_features||noise_table->size()!=size_t(3)*(1<<24)))throw std::runtime_error("invalid universal noise table");
  if(device||!d||!input||!w||!h||w%8||h%8||UINT64(w)*h/64>65535||fw.size()!=8736||aw.size()!=8225)throw std::runtime_error("invalid native preblock contract");
  if(temporal_input){if(!noise_table||raw_features||!live_profile||temporal_input->GetDesc().Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||temporal_input->GetDesc().Width<UINT64(w)*h*16)throw std::runtime_error("temporal preblock contract");temporal=temporal_input;temporal->AddRef();}
  const wchar_t*finish_flag=_wgetenv(L"DLSS5_TEST_COALESCED_FINISH");if(finish_flag&&wcscmp(finish_flag,L"0")&&wcscmp(finish_flag,L"1"))throw std::runtime_error("invalid coalesced finish flag");coalesced_finish=finish_flag&&!wcscmp(finish_flag,L"1");
  // FAST PATH (DLSS5_PREBLOCK_DOWN_ONLY): finish writes only the 2x2 pooled buffer; Main() is not produced (post70 reads RawTiles()).
  {const wchar_t*dv=_wgetenv(L"DLSS5_PREBLOCK_DOWN_ONLY");if(dv&&wcscmp(dv,L"0")&&wcscmp(dv,L"1"))throw std::runtime_error("invalid preblock down-only flag");down_only=coalesced_finish&&noise_table!=nullptr&&dv&&!wcscmp(dv,L"1");}
  // FAST PATH (DLSS5_PREBLOCK_MAIN8): finish writes Main() as E4M3 bytes (Main8()); post70 reads them (mode 7).
  {const wchar_t*m8=_wgetenv(L"DLSS5_PREBLOCK_MAIN8");if(m8&&wcscmp(m8,L"0")&&wcscmp(m8,L"1"))throw std::runtime_error("invalid preblock main8 flag");main8_mode=coalesced_finish&&!down_only&&noise_table!=nullptr&&m8&&!wcscmp(m8,L"1");}
  const wchar_t*prefix_flag=_wgetenv(L"DLSS5_TEST_SPLIT_PREBLOCK_FFN");if(prefix_flag&&wcscmp(prefix_flag,L"0")&&wcscmp(prefix_flag,L"1"))throw std::runtime_error("invalid split preblock flag");prefix_wave=!raw_features&&noise_table&&prefix_flag&&!wcscmp(prefix_flag,L"1");
  const wchar_t*ffn_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_FFN");if(ffn_flag&&wcscmp(ffn_flag,L"0")&&wcscmp(ffn_flag,L"1"))throw std::runtime_error("invalid wave C32 FFN flag");wave_ffn=raw_features&&ffn_flag&&!wcscmp(ffn_flag,L"1");
  if(wave_ffn||prefix_wave)for(size_t i=512;i<8704;i++){uint32_t b;std::memcpy(&b,&fw[i],4);uint32_t m=b&0x7fffffffu;if(m&&((m&0x1fffu)||(m>>23)<113||(m>>23)>142))throw std::runtime_error("C32 FFN weight not exact half");}
  const wchar_t*local_ffn=_wgetenv(L"DLSS5_TEST_WAVE_C32_FFN_LOCAL");if(local_ffn&&wcscmp(local_ffn,L"0")&&wcscmp(local_ffn,L"1"))throw std::runtime_error("invalid local C32 FFN flag");wave_ffn_local=wave_ffn&&local_ffn&&!wcscmp(local_ffn,L"1");
  device=d;width=w;height=h;UINT64 bytes=UINT64(w)*h*32*4;
  // DLSS5_TEST_SHARED_C32_SCRATCH=1: every serially executed C32 instance shares one ffn/raw pair sized by the first (largest) creator.
  if(const wchar_t*sc=_wgetenv(L"DLSS5_TEST_SHARED_C32_SCRATCH")){if(wcscmp(sc,L"0")&&wcscmp(sc,L"1"))throw std::runtime_error("invalid shared C32 scratch flag");shared_c32=!wcscmp(sc,L"1");}
  if(shared_c32){
   // First creator sizes the pair with shift-padding headroom so padded full-resolution stages fit too.
   if(!SharedFfn()){UINT64 cap=UINT64(w+8)*(h+8)*32*4;SharedFfn()=Buffer(cap,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);SharedRaw()=Buffer(cap,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);SharedBytes()=cap;}
   if(bytes>SharedBytes())throw std::runtime_error("shared C32 scratch smaller than this instance; create the largest instance first");
   // Down-only preblock: post70 reads this instance's raw tiles at the end of the frame, so they must not live in the shared scratch.
   ffn=SharedFfn();ffn->AddRef();if(down_only){private_raw=true;raw=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}else{raw=SharedRaw();raw->AddRef();}
  }else{ffn=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);raw=Buffer(bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}buffer_bytes=bytes;if(main8_mode)main8=Buffer(bytes/4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);down=Buffer(bytes/4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const std::vector<float>* values[]={&fw,&aw};
  for(UINT i=0;i<2;i++){weights[i]=Buffer(values[i]->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(weights[i]->Map(0,&none,&p));std::memcpy(p,values[i]->data(),values[i]->size()*4);weights[i]->Unmap(0,nullptr);}
  if(noise_table){noise=Buffer(noise_table->size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(noise->Map(0,&none,&p));std::memcpy(p,noise_table->data(),noise_table->size()*4);noise->Unmap(0,nullptr);}
  const wchar_t*resident_flag=_wgetenv(L"DLSS5_TEST_RESIDENT_NOISE");if(resident_flag&&wcscmp(resident_flag,L"0")&&wcscmp(resident_flag,L"1"))throw std::runtime_error("invalid resident noise flag");
  if(noise&&resident_flag&&!wcscmp(resident_flag,L"1")){auto*local=NativeResidentTable(device,noise);noise->Release();noise=local;}
  const wchar_t*weight_flag=_wgetenv(L"DLSS5_TEST_RESIDENT_C32_WEIGHTS");if(weight_flag&&wcscmp(weight_flag,L"0")&&wcscmp(weight_flag,L"1"))throw std::runtime_error("invalid resident C32 weights flag");
  const wchar_t*all_resident=_wgetenv(L"DLSS5_TEST_RESIDENT_WEIGHTS");
  if((weight_flag&&!wcscmp(weight_flag,L"1"))||(all_resident&&!wcscmp(all_resident,L"1")))for(auto*&w:weights){auto*local=NativeResidentTable(device,w);w->Release();w=local;}
  if(wave_ffn_local||prefix_wave){
   // Layout: expand f16 [128][32] @0, contract f16 [32][128] @8192, residual scale f32[32] @16384, contract E4M3 [32][128] @16512 (fast path).
   // FAST PATH 3 (NATIVE_C32_FFN_FAST3): E4M3 copy of the expand weights [128][32] at byte 20608 (tile-contiguous when DLSS5_C32_TILED_WEIGHTS=1).
   const wchar_t*fast3_flag=_wgetenv(L"DLSS5_C32_FFN_FAST3");if(fast3_flag&&wcscmp(fast3_flag,L"0")&&wcscmp(fast3_flag,L"1"))throw std::runtime_error("invalid C32 FFN fast3 flag");const bool fast3=fast3_flag&&!wcscmp(fast3_flag,L"1");
   std::vector<float>packed(4096+32+1024+(fast3?1024+768+512:0));
   if(fast3){ // prefix mix weights as two f16 B tiles [n][k 32][j 16] (k>=16 zero) at byte 27776 for the inline-prefix FFN (mode 5)
    unsigned char*bt=reinterpret_cast<unsigned char*>(packed.data())+27776;std::memset(bt,0,2048);
    for(size_t n=0;n<2;n++)for(size_t k=0;k<16;k++)for(size_t j=0;j<16;j++){float v=fw[(n*16+j)*16+k];uint32_t b;std::memcpy(&b,&v,4);uint32_t m=b&0x7fffffffu;uint16_t h=uint16_t((b>>16)&0x8000);if(m){int e=int(m>>23);if(e>=113){if((m&0x1fffu)||e>142)throw std::runtime_error("prefix weight not exact half");h|=uint16_t(((e-112)<<10)|((m&0x7fffff)>>13));}else{float q=std::fabs(v)*16777216.f;if(q!=std::floor(q)||q>1023.f)throw std::runtime_error("prefix weight not exact subnormal half");h|=uint16_t(q);}}std::memcpy(bt+(n*512+k*16+j)*2,&h,2);}
   }
   ffn_fast3=fast3;
   for(size_t i=0;i<8192;i++){uint32_t bits;std::memcpy(&bits,&fw[512+i],4);uint32_t m=bits&0x7fffffffu;uint16_t h=uint16_t((bits>>16)&0x8000);if(m)h|=uint16_t(((int(m>>23)-112)<<10)|((m&0x7fffff)>>13));std::memcpy(reinterpret_cast<unsigned char*>(packed.data())+i*2,&h,2);}
   std::memcpy(packed.data()+4096,fw.data()+8704,128);
   {unsigned char*o8=reinterpret_cast<unsigned char*>(packed.data())+16512;for(size_t i=0;i<4096;i++){float v=fw[4608+i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("C32 contract weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("C32 contract weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}}
   if(fast3){
    // Residual diagonals for the raw-tile chain (mode 3): per 16-column block three 32x16 E4M3 matrices at 24704, scale=s0+s1+s2.
    unsigned char*diag=reinterpret_cast<unsigned char*>(packed.data())+24704;std::memset(diag,0,3072);
    auto e4m3r=[](float v,float&decoded)->uint8_t{uint32_t b;std::memcpy(&b,&v,4);uint8_t sg=uint8_t((b>>24)&0x80u);float m=std::fabs(v);if(m<0.015625f){float q=std::nearbyint(m*512.f);if(q>7)q=7;decoded=(sg?-1.f:1.f)*q/512.f;return uint8_t(sg|uint8_t(q));}if(m>=448.f)throw std::runtime_error("C32 residual scale out of FP8 range");int e=int(std::floor(std::log2(m)));float step=std::ldexp(1.f,e-3);float q=std::nearbyint(m/step);if(q==16){q=8;e++;step*=2.f;}if(e+7<1||e+7>15)throw std::runtime_error("C32 residual scale part out of FP8 range");decoded=(sg?-1.f:1.f)*q*step;return uint8_t(sg|((e+7)<<3)|(uint8_t(q)&7));};
    for(size_t j=0;j<32;j++){float r=fw[8704+j];for(int part=0;part<3;part++){float dec;uint8_t byte=e4m3r(r,dec);r-=dec;diag[((j/16)*3+part)*512+j*16+(j%16)]=byte;}}
    unsigned char*o8=reinterpret_cast<unsigned char*>(packed.data())+20608;for(size_t i=0;i<4096;i++){float v=fw[512+i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("C32 expand weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("C32 expand weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}}
   if(const wchar_t*tw=_wgetenv(L"DLSS5_C32_TILED_WEIGHTS")){if(wcscmp(tw,L"0")&&wcscmp(tw,L"1"))throw std::runtime_error("invalid C32 tiled weights flag");if(!wcscmp(tw,L"1")){
    // FAST PATH (NATIVE_C32_TILED_WEIGHTS): expand f16 tiles [block][k 32][j 16] at block*1024; contract E4M3 tiles [block][g][k 32][j 16] at 16512+(block*4+g)*512.
    unsigned char*base=reinterpret_cast<unsigned char*>(packed.data());std::vector<unsigned char>t(8192);
    for(size_t block=0;block<8;block++)for(size_t k=0;k<32;k++)for(size_t j=0;j<16;j++)std::memcpy(t.data()+block*1024+(k*16+j)*2,base+((block*16+j)*32+k)*2,2);
    std::memcpy(base,t.data(),8192);
    std::vector<unsigned char>t8(4096);for(size_t block=0;block<2;block++)for(size_t g=0;g<4;g++)for(size_t k=0;k<32;k++)for(size_t j=0;j<16;j++)t8[(block*4+g)*512+k*16+j]=base[16512+(block*16+j)*128+g*32+k];
    std::memcpy(base+16512,t8.data(),4096);
    if(fast3){std::vector<unsigned char>e8(4096);for(size_t block=0;block<8;block++)for(size_t k=0;k<32;k++)for(size_t j=0;j<16;j++)e8[block*512+k*16+j]=base[20608+(block*16+j)*32+k];std::memcpy(base+20608,e8.data(),4096);}}}
   auto*u=Buffer(packed.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(u->Map(0,&none,&p));std::memcpy(p,packed.data(),packed.size()*4);u->Unmap(0,nullptr);
   auto*local=NativeResidentTable(device,u);u->Release();if(prefix_wave)prefix_ffn_weights=local;else{weights[0]->Release();weights[0]=local;}
  }
  root=Root(2,1,noise!=nullptr,temporal!=nullptr);finish_root=Root(1,2);
  if(const wchar_t*rs=_wgetenv(L"DLSS5_TEST_BLOCKED_C32_FFN")){const wchar_t*ro=_wgetenv(L"DLSS5_TEST_C32_FFN_RAW_STORE");blocked_ffn_raw_store=!wcscmp(rs,L"1")&&wave_ffn_local&&ro&&!wcscmp(ro,L"1");}
  if(blocked_ffn_raw_store){
   // Root-descriptor binding for the blocked FFN so the matrix Store targets a raw UAV.
   D3D12_ROOT_PARAMETER rp[3]{};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[0].Descriptor={0,0};rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor={1,0};rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[2].Descriptor={0,0};
   D3D12_ROOT_PARAMETER all[6]={rp[0],rp[1],rp[2],{},{},{}};all[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;all[3].Constants={0,0,13};
   // Mode 4 merge fold: t2 = skip residual, t3 = merge coefficients (root SRVs, bound only when (mapping[0]==4||mapping[0]==6||mapping[0]==7)).
   all[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;all[4].Descriptor={2,0};all[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;all[5].Descriptor={3,0};
   D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=6;rd.pParameters=all;ID3DBlob*rb=nullptr,*re=nullptr;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rb,&re));Check(device->CreateRootSignature(0,rb->GetBufferPointer(),rb->GetBufferSize(),IID_PPV_ARGS(&ffn_root)));rb->Release();if(re)re->Release();
  }
  stage_input=input;stage_input->AddRef();
  Heap(0,weights[0],weights[0]->GetDesc().Width,input,UINT64(w)*h*(raw_features?128:16),prefix_wave?raw:ffn,bytes,false);{
   // Packed attention weights (f16 QKV/projection + f32 bias/scales) read directly by wave loads.
   const wchar_t*local_attention=_wgetenv(L"DLSS5_TEST_LOCAL_C32_ATTENTION");if(local_attention&&wcscmp(local_attention,L"0")&&wcscmp(local_attention,L"1"))throw std::runtime_error("invalid local C32 attention flag");
   if(local_attention&&!wcscmp(local_attention,L"1")){
    // FAST PATH (attention fast2): E4M3 copy of the projection weights [32][32] at byte 24832.
    const wchar_t*attn2=_wgetenv(L"DLSS5_C32_ATTN_FAST2");if(attn2&&wcscmp(attn2,L"0")&&wcscmp(attn2,L"1"))throw std::runtime_error("invalid C32 attention fast2 flag");const bool attn_fast2=attn2&&!wcscmp(attn2,L"1");
    {const wchar_t*a3=_wgetenv(L"DLSS5_C32_ATTN_FAST3");if(a3&&wcscmp(a3,L"0")&&wcscmp(a3,L"1"))throw std::runtime_error("invalid C32 attention fast3 flag");attn_fast3=attn_fast2&&a3&&!wcscmp(a3,L"1");}
    {const wchar_t*a4=_wgetenv(L"DLSS5_C32_ATTN_FAST4");if(a4&&wcscmp(a4,L"0")&&wcscmp(a4,L"1"))throw std::runtime_error("invalid C32 attention fast4 flag");attn_fast4=attn_fast3&&a4&&!wcscmp(a4,L"1");}
    std::vector<float>packed(attn_fast2?7232:6177);unsigned char*bytes_out=reinterpret_cast<unsigned char*>(packed.data());
    // qkv fast2: E4M3 copy of the QKV weights [96][32] at byte 25856.
    if(attn_fast2){unsigned char*o8=bytes_out+25856;for(size_t i=0;i<3072;i++){float v=aw[i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("C32 QKV weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("C32 QKV weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}}
    if(attn_fast2){unsigned char*o8=bytes_out+24832;for(size_t i=0;i<1024;i++){float v=aw[3072+i];uint32_t b;std::memcpy(&b,&v,4);uint32_t a=b&0x7fffffffu;uint8_t sg=uint8_t((b>>24)&0x80u);if(!a){o8[i]=sg;continue;}float m=std::fabs(v);if(m<0.015625f){float q=m*512.f;if(q!=std::floor(q)||q>7)throw std::runtime_error("C32 projection weight not FP8-representable");o8[i]=uint8_t(sg|uint8_t(q));continue;}int e=int(a>>23)-127+7;if(e<1||e>15||(a&0xfffff)||(e==15&&((a>>20)&7)==7))throw std::runtime_error("C32 projection weight not FP8-representable");o8[i]=uint8_t(sg|(e<<3)|((a>>20)&7));}}
    for(size_t i=0;i<4096;i++){uint32_t b;std::memcpy(&b,&aw[i],4);uint32_t m=b&0x7fffffffu;uint16_t h=uint16_t((b>>16)&0x8000);if(m){if((m&0x1fffu)||(m>>23)<113||(m>>23)>142)throw std::runtime_error("C32 attention weights not exact normal half");h|=uint16_t(((int(m>>23)-112)<<10)|((m&0x7fffff)>>13));}std::memcpy(bytes_out+i*2,&h,2);}
    std::memcpy(bytes_out+8192,aw.data()+4096,4096*4);std::memcpy(bytes_out+24576,aw.data()+8192,33*4);
    auto*u=Buffer(packed.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void*p=nullptr;D3D12_RANGE none{};Check(u->Map(0,&none,&p));std::memcpy(p,packed.data(),packed.size()*4);u->Unmap(0,nullptr);
    attention_local=NativeResidentTable(device,u);u->Release();
   }
  }
  Heap(1,attention_local?attention_local:weights[1],attention_local?attention_local->GetDesc().Width:aw.size()*4,ffn,bytes,raw,bytes,false);
  {
   // Four-pass C32 attention (qkv / normalize / attention / projection) on root descriptors, reusing raw+main as scratch.
   const wchar_t*sa=_wgetenv(L"DLSS5_TEST_SPLIT_C32_ATTENTION");if(sa&&wcscmp(sa,L"0")&&wcscmp(sa,L"1"))throw std::runtime_error("invalid split C32 attention flag");
   split_attention=sa&&!wcscmp(sa,L"1");if(split_attention&&!attention_local)throw std::runtime_error("split C32 attention requires packed local attention weights");
   if(split_attention){
    D3D12_ROOT_PARAMETER rp[5]{};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[0].Descriptor={0,0};rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].Descriptor={1,0};rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[2].Descriptor={0,0};rp[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[3].Descriptor={1,0};rp[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[4].Constants={0,0,5};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=5;rd.pParameters=rp;ID3DBlob*rb=nullptr,*re=nullptr;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rb,&re));Check(device->CreateRootSignature(0,rb->GetBufferPointer(),rb->GetBufferSize(),IID_PPV_ARGS(&split_root)));rb->Release();if(re)re->Release();
    // FAST PATH: qkv+normalize and attention+projection fused (two dispatches instead of four).
    if(const wchar_t*fz=_wgetenv(L"DLSS5_C32_FUSED_ATTENTION")){if(wcscmp(fz,L"0")&&wcscmp(fz,L"1"))throw std::runtime_error("invalid fused C32 attention flag");fused_attention=!wcscmp(fz,L"1");}
    const wchar_t*wa=_wgetenv(L"DLSS5_C32_WAVE_ATTENTION");if(wa&&wcscmp(wa,L"0")&&wcscmp(wa,L"1"))throw std::runtime_error("invalid C32 wave attention flag");wave_attention=fused_attention&&wa&&!wcscmp(wa,L"1");
    const wchar_t*names[]={fused_attention?L"\\native_wave_c32_fused_qkv.cso":L"\\native_wave_c32_split_qkv.cso",L"\\native_wave_c32_split_normalize.cso",wave_attention?L"\\native_wave_c32_fused_attention_wave.cso":fused_attention?L"\\native_wave_c32_fused_attention.cso":L"\\native_wave_c32_split_attention.cso",L"\\native_wave_c32_split_projection.cso"};
    for(UINT i=0;i<4;i++){if(fused_attention&&(i&1))continue;ID3DBlob*code=nullptr;Check(D3DReadFileToBlob((shader_dir+names[i]).c_str(),&code));D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=split_root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};auto hr=device->CreateComputePipelineState(&p,IID_PPV_ARGS(&split_pso[i]));code->Release();Check(hr);}
   }
  }if(main8_mode)Heap(2,raw,bytes,main8,bytes/4,down,bytes/4,true);
  const wchar_t*flag=_wgetenv(L"DLSS5_TEST_SHARED_C32");if(flag&&wcscmp(flag,L"0")&&wcscmp(flag,L"1"))throw std::runtime_error("invalid shared C32 flag");shared_raw=raw_features&&flag&&!wcscmp(flag,L"1");
  const wchar_t* names[]={L"preblock_input_mix.hlsl",L"preblock_attention_core.hlsl",L"preblock_finish.hlsl"};
  auto count=std::to_string(UINT64(w)*h*32);
  const wchar_t*cache_flag=_wgetenv(L"DLSS5_TEST_CACHE_C32_INPUT");if(cache_flag&&wcscmp(cache_flag,L"0")&&wcscmp(cache_flag,L"1"))throw std::runtime_error("invalid cached C32 input flag");
  const wchar_t*pad_flag=_wgetenv(L"DLSS5_TEST_PAD_C32_LDS");if(pad_flag&&wcscmp(pad_flag,L"0")&&wcscmp(pad_flag,L"1"))throw std::runtime_error("invalid C32 LDS padding flag");
  // FAST PATH: input-mix prefix as a plain float dot + f16 round instead of the integer-emulated HMMA.
  const wchar_t*fp=_wgetenv(L"DLSS5_FAST_PREFIX");if(fp&&wcscmp(fp,L"0")&&wcscmp(fp,L"1"))throw std::runtime_error("invalid fast prefix flag");const bool fast_prefix=fp&&!wcscmp(fp,L"1");
  // FAST PATH: wave-matrix prefix mix (native_wave_prefix.cso, dxc-compiled; live profile + noise + prefix-only contract baked in).
  {const wchar_t*wp=_wgetenv(L"DLSS5_WAVE_PREFIX");if(wp&&wcscmp(wp,L"0")&&wcscmp(wp,L"1"))throw std::runtime_error("invalid wave prefix flag");wave_prefix=wp&&!wcscmp(wp,L"1")&&prefix_wave&&fast_prefix&&live_profile&&!raw_features&&noise;
   const wchar_t*ip=_wgetenv(L"DLSS5_INLINE_PREFIX");if(ip&&wcscmp(ip,L"0")&&wcscmp(ip,L"1"))throw std::runtime_error("invalid inline prefix flag");inline_prefix=ip&&!wcscmp(ip,L"1")&&prefix_wave&&fast_prefix&&live_profile&&!raw_features&&noise&&ffn_fast3;if(inline_prefix)mapping[0]=5;}
  if(prefix_wave)Heap(3,prefix_ffn_weights,prefix_ffn_weights->GetDesc().Width,inline_prefix?stage_input:raw,inline_prefix?UINT64(w)*h*16:bytes,ffn,bytes,false);
  const wchar_t*fast_flag=_wgetenv(L"DLSS5_TEST_FAST_C32_FP8");if(fast_flag&&wcscmp(fast_flag,L"0")&&wcscmp(fast_flag,L"1"))throw std::runtime_error("invalid fast C32 flag");
  D3D_SHADER_MACRO macros[]={{"TOTAL_OUTPUTS",count.c_str()},{"FULL_FFN","1"},{"NATIVE_PREFIX_ONLY",prefix_wave?"1":"0"},{"RAW_OUTPUT","1"},{"RAW_INPUT",raw_features?"1":"0"},{"DEBUG_FEATURES","0"},{"DYNAMIC_PARAMETERS","1"},{"LIVE_PROFILE",live_profile?"1":"0"},{"NOISE_SEED","0"},{"NATIVE_NOISE_TABLE",noise?"1":"0"},{"NATIVE_TEMPORAL_RGB",temporal?"1":"0"},{"NATIVE_FAST_C32_FP8",fast_flag&&!wcscmp(fast_flag,L"1")?"1":"0"},{"NATIVE_PAD_C32_LDS",pad_flag&&!wcscmp(pad_flag,L"1")?"1":"0"},{"NATIVE_CACHE_C32_INPUT",cache_flag&&!wcscmp(cache_flag,L"1")?"1":"0"},{"NATIVE_FAST_PREFIX",fast_prefix?"1":"0"},{nullptr,nullptr}};
  const wchar_t*wave_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_SCORES");if(wave_flag&&wcscmp(wave_flag,L"0")&&wcscmp(wave_flag,L"1"))throw std::runtime_error("invalid wave C32 flag");
  const wchar_t*qkv_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_QKV");if(qkv_flag&&wcscmp(qkv_flag,L"0")&&wcscmp(qkv_flag,L"1"))throw std::runtime_error("invalid wave C32 QKV flag");
  const bool wave_qkv=qkv_flag&&!wcscmp(qkv_flag,L"1");if(wave_qkv&&(!wave_flag||wcscmp(wave_flag,L"1")))throw std::runtime_error("wave QKV requires wave scores");
  const wchar_t*av_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_AV");if(av_flag&&wcscmp(av_flag,L"0")&&wcscmp(av_flag,L"1"))throw std::runtime_error("invalid wave C32 AV flag");const bool wave_av=av_flag&&!wcscmp(av_flag,L"1");if(wave_av&&!wave_qkv)throw std::runtime_error("wave AV requires wave QKV");
  const wchar_t*proj_flag=_wgetenv(L"DLSS5_TEST_WAVE_C32_PROJECTION");if(proj_flag&&wcscmp(proj_flag,L"0")&&wcscmp(proj_flag,L"1"))throw std::runtime_error("invalid wave C32 projection flag");const bool wave_projection=proj_flag&&!wcscmp(proj_flag,L"1");if(wave_projection&&!wave_av)throw std::runtime_error("wave projection requires AV");
  if(wave_qkv)for(size_t i=0;i<(wave_projection?4096:3072);i++){uint32_t b;std::memcpy(&b,&aw[i],4);uint32_t m=b&0x7fffffffu;if(m&&((m&0x1fffu)||(m>>23)<113||(m>>23)>142))throw std::runtime_error("C32 QKV weights not exact normal half");}
  const wchar_t*blocked_flag=_wgetenv(L"DLSS5_TEST_BLOCKED_C32_FFN");if(blocked_flag&&wcscmp(blocked_flag,L"0")&&wcscmp(blocked_flag,L"1"))throw std::runtime_error("invalid blocked C32 FFN flag");const bool blocked_ffn=blocked_flag&&!wcscmp(blocked_flag,L"1");
  const wchar_t*local_ffn_file=blocked_ffn?L"\\native_wave_c32_ffn_blocked.cso":L"\\native_wave_c32_ffn_local.cso";
  if(prefix_wave){ID3DBlob*code=nullptr;Check(D3DReadFileToBlob((shader_dir+local_ffn_file).c_str(),&code));D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=blocked_ffn_raw_store?ffn_root:root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};auto hr=device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso[3]));code->Release();Check(hr);}
  for(UINT i=0;i<3;i++){
   ID3DBlob*code=nullptr,*error=nullptr;auto path=shader_dir+L"\\"+names[i];HRESULT hr=(i==0&&wave_prefix)?D3DReadFileToBlob((shader_dir+L"\\native_wave_prefix.cso").c_str(),&code):(i==0&&wave_ffn)?D3DReadFileToBlob((shader_dir+(wave_ffn_local?local_ffn_file:L"\\native_wave_c32_ffn.cso")).c_str(),&code):(i==1&&wave_flag&&!wcscmp(wave_flag,L"1"))?D3DReadFileToBlob((shader_dir+(wave_projection?L"\\native_wave_c32_full_attention.cso":wave_av?L"\\native_wave_c32_av.cso":wave_qkv?L"\\native_wave_c32_qkv.cso":L"\\native_wave_c32_scores.cso")).c_str(),&code):D3DCompileFromFile(path.c_str(),macros,D3D_COMPILE_STANDARD_FILE_INCLUDE,(i==2&&coalesced_finish)?(down_only?"finish_down":main8_mode?"finish_main8":"finish_coalesced"):(i==0&&shared_raw)?"raw_ffn_shared":"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
   if(FAILED(hr)){std::string message=error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"compile failed";if(error)error->Release();throw std::runtime_error(message);}
   if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=i==2?finish_root:(i==0&&blocked_ffn_raw_store&&!prefix_wave)?ffn_root:root;p.CS={code->GetBufferPointer(),code->GetBufferSize()};Check(device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso[i])));code->Release();
  }
 }
 void Record(ID3D12GraphicsCommandList*c,UINT seed,bool local_oracle=false,bool temporal_enabled=false,NativeNetworkTimestamps*timer=nullptr,const char*label="c32_probe"){
  if(!device||!c)throw std::runtime_error("native preblock not created");
  if(shared_c32){
   if(SharedFfnReadable()){Barrier(c,ffn,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);SharedFfnReadable()=false;}
   // Mode 3 mapping reads the (shared) raw tiles of the previous stage in the FFN; keep them readable until the attention stage.
   if(private_raw){if(recorded)Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
   else if(SharedRawReadable()&&!(mapping[0]==3&&mapped_source==raw)){Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);SharedRawReadable()=false;}
   if(recorded)for(auto*r:{main8_mode?main8:main,down})if(r)Barrier(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }else if(recorded)for(auto*r:{ffn,raw,main8_mode?main8:main,down})if(r)Barrier(c,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if(temporal_enabled&&!temporal)throw std::runtime_error("temporal input not bound");
  const UINT constants[]={seed,width,height,local_oracle?1u:0u,temporal_enabled?1u:0u,mapping[0],mapping[1],mapping[2],mapping[3],mapping[4],mapping[5],mapping[6],mapping[7]};const UINT groups=width*height/64;
  for(UINT stage=0;stage<3;stage++){
   if(stage==2&&skip_finish){if(timer)timer->Mark(c,std::string(label)+"_stage2");continue;}
   if(stage==1&&shared_c32&&!private_raw&&SharedRawReadable()){Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);SharedRawReadable()=false;}
   if(stage==1&&split_attention){
    c->SetComputeRootSignature(split_root);c->SetComputeRootShaderResourceView(0,attention_local->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,ffn->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,raw->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,(attn_fast3?raw:Main())->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,5,constants,0);
    D3D12_RESOURCE_BARRIER uav[2]{};for(UINT k=0;k<2;k++){uav[k].Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;uav[k].UAV.pResource=k?(attn_fast3?raw:main):raw;}
    const UINT tokens=width*height;const UINT g16=tokens/16;
    if(!attn_fast3){c->SetPipelineState(split_pso[0]);c->Dispatch(g16<65535?g16:65535,(g16+65534)/65535,1);c->ResourceBarrier(2,uav);}if(timer)timer->Mark(c,std::string(label)+"_qkv");
    if(fused_attention){c->SetPipelineState(split_pso[2]);if(attn_fast4){UINT n=(tokens/64+1)/2;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else if(wave_attention){UINT w=tokens/64;c->Dispatch(w<65535?w:65535,(w+65534)/65535,1);}else c->Dispatch(tokens/64,1,1);}
    else{
    c->SetPipelineState(split_pso[1]);c->Dispatch(tokens<65535?tokens:65535,(tokens+65534)/65535,1);c->ResourceBarrier(1,uav);
    c->SetPipelineState(split_pso[2]);c->Dispatch(tokens/64,1,1);c->ResourceBarrier(2,uav);
    c->SetPipelineState(split_pso[3]);c->Dispatch(g16<65535?g16:65535,(g16+65534)/65535,1);
    }
   }else if(stage==0&&blocked_ffn_raw_store&&!prefix_wave){
    c->SetComputeRootSignature(ffn_root);c->SetComputeRootShaderResourceView(0,weights[0]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,(mapped_source?mapped_source:stage_input)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,ffn->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,13,constants,0);if((mapping[0]==4||mapping[0]==6||mapping[0]==7)){c->SetComputeRootShaderResourceView(4,merge_skip->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(5,merge_coeff->GetGPUVirtualAddress());}c->SetPipelineState(pso[0]);UINT n=groups*4;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);
   }else if(stage==0&&inline_prefix){
   }else{
   if(stage==2&&!heap[2]){Main();Heap(2,raw,buffer_bytes,main,buffer_bytes,down,buffer_bytes/4,true);}
   c->SetDescriptorHeaps(1,&heap[stage]);c->SetComputeRootSignature(stage==2?finish_root:root);c->SetComputeRootDescriptorTable(0,heap[stage]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,13,constants,0);if(noise&&stage<2)c->SetComputeRootShaderResourceView(2,noise->GetGPUVirtualAddress());if(temporal&&stage<2)c->SetComputeRootShaderResourceView(3,temporal->GetGPUVirtualAddress());c->SetPipelineState(pso[stage]);if(stage==2&&coalesced_finish){UINT n=down_only?groups*8:main8_mode?groups*8:groups*32;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else if(stage==0&&wave_prefix){UINT n=width*height/16;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else if(stage==0&&wave_ffn){UINT n=groups*4;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else if(stage==0&&shared_raw){UINT n=groups*8;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);}else c->Dispatch(groups,1,1);
   }
   if(stage==0&&prefix_wave){
    Barrier(c,raw,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);if(timer)timer->Mark(c,std::string(label)+"_prefix");
    if(test_prefix_readback){Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);c->CopyBufferRegion(test_prefix_readback,0,raw,0,4096);Barrier(c,raw,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    if(blocked_ffn_raw_store){c->SetComputeRootSignature(ffn_root);c->SetComputeRootShaderResourceView(0,prefix_ffn_weights->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,(inline_prefix?stage_input:raw)->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(2,ffn->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(3,13,constants,0);if(inline_prefix){c->SetComputeRootShaderResourceView(4,(temporal?temporal:stage_input)->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(5,prefix_ffn_weights->GetGPUVirtualAddress());}}else{c->SetDescriptorHeaps(1,&heap[3]);c->SetComputeRootSignature(root);c->SetComputeRootDescriptorTable(0,heap[3]->GetGPUDescriptorHandleForHeapStart());c->SetComputeRoot32BitConstants(1,13,constants,0);if(inline_prefix){if(noise)c->SetComputeRootShaderResourceView(2,noise->GetGPUVirtualAddress());if(temporal)c->SetComputeRootShaderResourceView(3,temporal->GetGPUVirtualAddress());}}c->SetPipelineState(pso[3]);UINT n=groups*4;c->Dispatch(n<65535?n:65535,(n+65534)/65535,1);
    Barrier(c,raw,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);Barrier(c,ffn,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   }else if(stage<2)Barrier(c,stage?raw:ffn,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   if(timer)timer->Mark(c,std::string(label)+"_stage"+std::to_string(stage));
  }
  if(main8_mode?main8:main)Barrier(c,main8_mode?main8:main,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Barrier(c,down,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);recorded=true;if(shared_c32){SharedFfnReadable()=true;if(!private_raw)SharedRawReadable()=true;}
 }
 void CapturePrefixForTest(ID3D12Resource*r){if(!prefix_wave||test_prefix_readback||!r||r->GetDesc().Width<4096)throw std::runtime_error("prefix capture test contract");D3D12_HEAP_PROPERTIES hp{};D3D12_HEAP_FLAGS flags{};Check(r->GetHeapProperties(&hp,&flags));if(hp.Type!=D3D12_HEAP_TYPE_READBACK)throw std::runtime_error("prefix capture requires readback");test_prefix_readback=r;r->AddRef();}
 ID3D12Resource* FfnTilesForTest()const{return ffn;}
 // FAST PATH: the FFN reads its input through a mapping instead of a pre-packed work buffer (see native_wave_c32_ffn_blocked.hlsl).
 ID3D12Resource*merge_skip{},*merge_coeff{};
 void MapMerge(ID3D12Resource*low,ID3D12Resource*skip,ID3D12Resource*coeff,UINT src_w,UINT src_h,UINT sx,UINT sy,UINT skip_mode=4,UINT psx=0,UINT psy=0,UINT pww=0){MapInput(low,skip_mode,src_w,src_h,sx,sy,psx,psy,pww);merge_skip=skip;merge_coeff=coeff;}
 void MapInput(ID3D12Resource*src,UINT mode,UINT src_w,UINT src_h,UINT sx,UINT sy,UINT psx,UINT psy,UINT pww){if(!blocked_ffn_raw_store||!wave_ffn_local)throw std::runtime_error("mapped C32 input requires the blocked raw-store FFN");mapped_source=src;UINT m[]={mode,src_w,src_h,sx,sy,psx,psy,pww};std::memcpy(mapping,m,sizeof(m));}
 bool RawStoreFfn()const{return blocked_ffn_raw_store;}
 // FAST PATH: consumers that only read RawTiles() (post70) skip the finish stage.
 bool skip_finish{},down_only{},main8_mode{},private_raw{},wave_prefix{},attn_fast3{},attn_fast4{},ffn_fast3{},inline_prefix{};void SetSkipFinish(bool v){skip_finish=v;}
 // Main() is allocated on first use: with MAIN8, chain-raw or skip-finish nobody reads it (saves one full f32 raster per stage).
 ID3D12Resource* Main()const{if(!main){auto*self=const_cast<NativePreblockRuntime*>(this);self->main=self->Buffer(buffer_bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}return main;}bool DownOnly()const{return down_only;}bool Main8Mode()const{return main8_mode;}ID3D12Resource* Main8()const{return main8;}UINT WorkWidth()const{return width;}ID3D12Resource* Downsample()const{return down;}ID3D12Resource* RawTiles()const{return raw;}
};
