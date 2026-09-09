#pragma once
#include "native_pinned_resource.h"
#include "native_resident_table.h"
#include "native_c32_stage.h"
#include "native_shader_cache.h"
// Pixel-aligned RGB base, texture-mask1/rgb-mode1 only. No optional blend textures.
class NativePost70 {
 NativeC32Stage body;
 ID3D12Resource *main_input{},*skip_input{},*color_input{},*merged{},*output{},*coefficients[2]{};
 ID3D12RootSignature*root{};ID3D12PipelineState*pso[2]{};
 UINT geometry[6]{};bool recorded{},direct{},merge_fold{},fast_rgb{};
 static void ck(HRESULT hr){if(FAILED(hr))throw std::runtime_error("post70 HRESULT="+std::to_string(unsigned(hr)));}
 static ID3D12Resource*buffer(ID3D12Device*d,UINT64 bytes,const std::vector<float>*data=nullptr){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=data?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=data?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ID3D12Resource*r=nullptr;ck(NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&desc,data?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));
  if(data){void*p=nullptr;D3D12_RANGE none{};ck(r->Map(0,&none,&p));std::memcpy(p,data->data(),bytes);r->Unmap(0,nullptr);r=NativeMaybeResident(d,r);}return r;
 }
 static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,bool begin){
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,begin?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,begin?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};c->ResourceBarrier(1,&b);
 }
public:
 NativePost70()=default;NativePost70(const NativePost70&)=delete;
 ~NativePost70(){for(auto*r:{main_input,skip_input,color_input,merged,output,coefficients[0],coefficients[1]})if(r)r->Release();if(root)root->Release();for(auto*p:pso)if(p)p->Release();}
 void Create(ID3D12Device*d,ID3D12Resource*main,ID3D12Resource*skip,ID3D12Resource*color,UINT width,UINT height,const std::vector<float>&scales,const std::vector<float>&ffn,const std::vector<float>&attention,const std::vector<float>&head,const std::wstring&dir,float input_scale=.03125f,UINT shift=0,UINT skip_mode=4,UINT skip_raw_work_width=0,UINT low_raw_work_width=0,UINT low_shift_x=0,UINT low_shift_y=0,UINT low_mode=8){
  if(shift>3)throw std::runtime_error("post shift contract");
  if(main_input||!d||!main||!skip||!color||width<16||height<16||((width>512||height>512)&&!(width==1920&&height==1152))||width%16||height%16||scales.size()!=64||head.size()!=96)throw std::runtime_error("post70 contract");
  UINT64 pixels=UINT64(width)*height;
  if(main->GetDesc().Width<(low_mode==9&&low_raw_work_width?pixels*8:pixels*8*4)||skip->GetDesc().Width<(skip_mode==7?pixels*32:pixels*32*4)||color->GetDesc().Width<pixels*4*4)throw std::runtime_error("post70 input capacity");
  main_input=main;skip_input=skip;color_input=color;for(auto*r:{main_input,skip_input,color_input})r->AddRef();geometry[0]=width;geometry[1]=height;std::memcpy(&geometry[2],&input_scale,4);
  {const char*pd=std::getenv("DLSS5_POST70_DIRECT");direct=pd&&!std::strcmp(pd,"1");const char*mf=std::getenv("DLSS5_POST70_MERGE_FOLD");merge_fold=direct&&mf&&!std::strcmp(mf,"1");}
  merged=buffer(d,merge_fold?16:pixels*32*4);output=buffer(d,pixels*3*4); /* merge fold never writes merged */coefficients[0]=buffer(d,scales.size()*4,&scales);coefficients[1]=buffer(d,head.size()*4,&head);
  {const wchar_t*o8=_wgetenv(L"DLSS5_POST70_OUT8");NativePreblockRuntime::PendingOut8()=o8&&!wcscmp(o8,L"1");}
  body.Create(d,merged,width,height,shift,ffn,attention,dir,true,direct||merge_fold);
  // FAST PATH (DLSS5_POST70_DIRECT): FFN reads the merged raster through the mapping (no pack), finish stage skipped
  // (rgb only needs the raw tiles), rgb head indexes the tile-major raw work buffer directly (no crop).
  {const char*pd=std::getenv("DLSS5_POST70_DIRECT");if(pd&&std::strcmp(pd,"0")&&std::strcmp(pd,"1"))throw std::runtime_error("invalid post70 direct flag");direct=pd&&!std::strcmp(pd,"1");}
  {const char*mf=std::getenv("DLSS5_POST70_MERGE_FOLD");if(mf&&std::strcmp(mf,"0")&&std::strcmp(mf,"1"))throw std::runtime_error("invalid post70 merge fold flag");merge_fold=direct&&mf&&!std::strcmp(mf,"1");}
  // skip_raw_work_width!=0: skip is the preblock's unquantized raw tile buffer (DLSS5_PREBLOCK_DOWN_ONLY), mode 6 in the FFN.
  if(skip_mode!=4&&!merge_fold)throw std::runtime_error("non-raster skip needs the post70 merge fold");
  // low_raw_work_width!=0 (DLSS5_POST70_LOW_RAW): main is the previous C32 stage's raw tile buffer (low_mode 8) or main8 (low_mode 9); needs skip_mode 7.
  if(low_raw_work_width&&(skip_mode!=7||!merge_fold))throw std::runtime_error("post70 low raw needs main8 skip and the merge fold");
  if(merge_fold){body.MapMerge(main_input,skip_input,coefficients[0],low_raw_work_width?low_mode:skip_mode,low_raw_work_width?low_raw_work_width:skip_raw_work_width,low_shift_x,low_shift_y);body.SetCropNeeded(false);body.SetSkipFinish(true);}
  else if(direct){body.MapFromRaster(merged);body.SetCropNeeded(false);body.SetSkipFinish(true);}
  geometry[3]=body.WorkWidth();geometry[4]=body.ShiftX();geometry[5]=body.ShiftY();
  D3D12_ROOT_PARAMETER params[5]{};for(UINT i=0;i<3;i++){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i;}
  params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[4].Constants={0,0,6};D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=5;desc.pParameters=params;
  ID3DBlob*blob=nullptr,*error=nullptr;ck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ck(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));blob->Release();if(error)error->Release();
const char*diagnostic=std::getenv("DLSS5_POST_BASE_ONLY");
  {const char*fr=std::getenv("DLSS5_POST70_FAST_RGB");if(fr&&std::strcmp(fr,"0")&&std::strcmp(fr,"1"))throw std::runtime_error("invalid post70 fast rgb flag");fast_rgb=fr&&!std::strcmp(fr,"1")&&!diagnostic;}
  if(body.AttnOut8()&&!(merge_fold&&fast_rgb))throw std::runtime_error("post70 out8 needs merge fold and the fast rgb head");
  const char*entries[]={"merge",fast_rgb?"finish_fast":"finish"};
    if(diagnostic&&std::strcmp(diagnostic,"1")&&std::strcmp(diagnostic,"2"))throw std::runtime_error("post diagnostic mode must be1 or2");
  D3D_SHADER_MACRO macros[]={{"POST_BASE_ONLY",diagnostic?diagnostic:"0"},{"POST_TILED_INPUT",direct?"1":"0"},{"POST_FAST_RGB",fast_rgb?"1":"0"},{"POST_INPUT8",body.AttnOut8()?"1":"0"},{"POST_INPUT_HALF",body.HalfStream()?"1":"0"},{nullptr,nullptr}};
  for(UINT i=0;i<2;i++){if(i==0&&merge_fold)continue;blob=nullptr;error=nullptr;auto hr=CompileNativeShader(dir+L"\\native_post70.hlsl",macros,entries[i],&blob,&error);if(FAILED(hr)){std::string message=error?std::string((char*)error->GetBufferPointer(),error->GetBufferSize()):"post70 compile";if(error)error->Release();throw std::runtime_error(message);}if(error)error->Release();D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};ck(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso[i])));blob->Release();}
 }
 UINT isolate_part{};void SetIsolatePart(UINT p){isolate_part=p;body.SetIsolateStage(p<4?p:0);} /* 1..3 body stages, 4 = rgb head only */
 void Record(ID3D12GraphicsCommandList*c,NativeNetworkTimestamps*timer=nullptr){
  if(!output||!c)throw std::runtime_error("post70 not created");if(recorded){if(!merge_fold)barrier(c,merged,true);barrier(c,output,true);}
  auto pass=[&](UINT i,ID3D12Resource*src,ID3D12Resource*extra,ID3D12Resource*dst,UINT planes){c->SetComputeRootSignature(root);c->SetPipelineState(pso[i]);c->SetComputeRootShaderResourceView(0,src->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(1,coefficients[i]->GetGPUVirtualAddress());c->SetComputeRootShaderResourceView(2,extra->GetGPUVirtualAddress());c->SetComputeRootUnorderedAccessView(3,dst->GetGPUVirtualAddress());c->SetComputeRoot32BitConstants(4,6,geometry,0);c->Dispatch(geometry[0]*geometry[1]/64,planes,1);};
  if(timer)timer->Mark(c,"post70_begin");if(!merge_fold){pass(0,main_input,skip_input,merged,32);barrier(c,merged,false);}if(timer)timer->Mark(c,"post70_merge");if(isolate_part!=4)body.Record(c,timer,"post70_detail");if(timer)timer->Mark(c,"post70_body");if(!isolate_part||isolate_part==4)pass(1,body.AttnOut8()?body.Main8():direct?body.RawWork():body.Output(),color_input,output,fast_rgb?1:3);barrier(c,output,false);if(timer)timer->Mark(c,"post70_rgb");recorded=true;
 }
 ID3D12Resource*Output()const{return output;}
 ID3D12Resource*Merged()const{return merged;}
 ID3D12Resource*Features()const{return body.Output();}
};
