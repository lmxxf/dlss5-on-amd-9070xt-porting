#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <fstream>
#include <vector>
#include <memory>
#include <chrono>
#include <cmath>
#include "native_vit_block.h"
#include "native_game_submission.h"
#include "native_lab_paths.h"
extern "C" {__declspec(dllexport) extern const UINT D3D12SDKVersion=721;__declspec(dllexport) const char*D3D12SDKPath=".\\D3D12\\";}
static void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT="+std::to_string(unsigned(h)));}
static void env(const char*k,const char*v){_putenv_s(k,v);std::wstring a(k,k+strlen(k)),b(v,v+strlen(v));_wputenv_s(a.c_str(),b.c_str());}
static void flags(const wchar_t*p){std::ifstream f(p);if(!f)throw std::runtime_error("flags");std::string s;while(std::getline(f,s)){if(s.rfind("DLSS5_",0))continue;auto n=s.find('=');if(n==s.npos)continue;while(!s.empty()&&(s.back()=='\r'||s.back()==' '))s.pop_back();env(s.substr(0,n).c_str(),s.substr(n+1).c_str());}}
static ID3D12Resource*buffer(ID3D12Device*d,UINT64 n,D3D12_HEAP_TYPE t){D3D12_HEAP_PROPERTIES hp{};hp.Type=t;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=n;b.Height=1;b.DepthOrArraySize=b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ID3D12Resource*r{};ck(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,t==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;}
int wmain(int argc,wchar_t**argv){try{
 if(argc!=6)throw std::runtime_error("usage fourstage ASSETS FLAGS ACTUAL_VIT31_INPUT_F32 TOKENS OUTPUT_PREFIX");
 flags(argv[2]);env("DLSS5_VIT_FUSED_FFN","0");env("DLSS5_MAKE_RESIDENT_EVERY","");const UINT tokens=wcstoul(argv[4],nullptr,10);if(tokens!=640)throw std::runtime_error("scope640 only");const size_t values=size_t(tokens)*1024,bytes=values*4;
 std::ifstream f(argv[3],std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(bytes))throw std::runtime_error("requires actual captured640x1024 F32 block31 input; no synthetic substitution");std::vector<float>input(values);f.seekg(0);f.read((char*)input.data(),bytes);for(float v:input)if(!std::isfinite(v))throw std::runtime_error("nonfinite input");
 const IID experimental={0x76f5573e,0xf13a,0x40f5,{0xb2,0x97,0x81,0xce,0x9e,0x18,0x93,0x3f}};ck(D3D12EnableExperimentalFeatures(1,&experimental,nullptr,nullptr));IDXGIFactory6*factory{};ck(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));ID3D12Device*d{};
 for(UINT i=0;;i++){IDXGIAdapter1*a{};if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&a))==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};a->GetDesc1(&desc);if(desc.VendorId==0x1002)ck(D3D12CreateDevice(a,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));a->Release();if(d)break;}factory->Release();if(!d)throw std::runtime_error("AMD required");ID3D12CommandQueue*q{};D3D12_COMMAND_QUEUE_DESC qd{};ck(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
 {NativeGameSubmission submit;submit.Create(q);ID3D12Resource*in=buffer(d,bytes,D3D12_HEAP_TYPE_UPLOAD),*rb=buffer(d,bytes,D3D12_HEAP_TYPE_READBACK);void*p{};D3D12_RANGE none{};ck(in->Map(0,&none,&p));memcpy(p,input.data(),bytes);in->Unmap(0,nullptr);
 {std::unique_ptr<NativeVitBlock> owned(new NativeVitBlock);NativeVitBlock&block=*owned;struct DrainBeforeRelease{NativeGameSubmission&s;std::unique_ptr<NativeVitBlock>&owner;~DrainBeforeRelease()noexcept{try{s.Flush();}catch(...){owner.release();fprintf(stderr,"retain fourstage resources after failed drain\n");}}}drain{submit,owned};std::wstring dir=argv[1],prefix=argv[5];auto w=[&](const wchar_t*n){return NativeReadF32(dir+L"\\"+n,"original model weights");};block.Create(d,in,tokens,w(L"block31-expand.f32"),w(L"block31-contract.f32"),w(L"block31-qkv.f32"),w(L"block31-projection.f32"),dir);NativeResidentFlush();if(block.DiagnosticAttentionOutput()->GetDesc().Width<bytes)throw std::runtime_error("attention output extent");
 auto read=[&](const wchar_t*label){submit.Submit([&](ID3D12GraphicsCommandList*c){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={block.DiagnosticAttentionOutput(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};c->ResourceBarrier(1,&b);c->CopyBufferRegion(rb,0,block.DiagnosticAttentionOutput(),0,bytes);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);c->ResourceBarrier(1,&b);});submit.Flush();std::vector<float>v(values);D3D12_RANGE range{0,bytes};ck(rb->Map(0,&range,&p));memcpy(v.data(),p,bytes);rb->Unmap(0,&none);for(float x:v)if(!std::isfinite(x))throw std::runtime_error("nonfinite output");FILE*out=_wfopen((prefix+L"-"+label+L".f32").c_str(),L"wb");if(!out||fwrite(v.data(),4,values,out)!=values)throw std::runtime_error("output");fclose(out);return v;};

 if(!std::getenv("FOURSTAGE_TIME_PAIR")){
 unsigned separateStages=0,batchStages=0;for(UINT s=0;s<4;s++)submit.Submit([&](ID3D12GraphicsCommandList*c){block.RecordStage(c,s);++separateStages;});auto a=read(L"separate");
 submit.Submit([&](ID3D12GraphicsCommandList*c){for(UINT s=0;s<4;s++){block.RecordStage(c,s);++batchStages;}});auto b=read(L"batch");size_t diff=0;double max=0;for(size_t i=0;i<values;i++){diff+=memcmp(&a[i],&b[i],4)!=0;max=std::max(max,std::abs(double(a[i])-b[i]));}
 printf("FOURSTAGE separate_record_calls=%u batch_record_calls=%u source_stage_indices=0,1,2,3 fusedFFN=0 bitdiff=%zu maxabs=%.9g finite=1 no_performance=1\n",separateStages,batchStages,diff,max);if(separateStages!=4||batchStages!=4||diff)throw std::runtime_error("gold/count failure");
 }else{
 unsigned calls=0;auto run4=[&](bool batch){if(batch)submit.Submit([&](ID3D12GraphicsCommandList*c){for(UINT k=0;k<4;k++){block.RecordStage(c,k);++calls;}});else for(UINT k=0;k<4;k++)submit.Submit([&](ID3D12GraphicsCommandList*c){block.RecordStage(c,k);++calls;});};
 run4(true);auto warm=read(L"warm-check");const char*expected=std::getenv("FOURSTAGE_EXPECTED");if(!expected)throw std::runtime_error("timing requires prior gold file");std::ifstream oracle(expected,std::ios::binary|std::ios::ate);if(!oracle||oracle.tellg()!=std::streamoff(bytes))throw std::runtime_error("gold reference size");std::vector<float>ref(values);oracle.seekg(0);oracle.read((char*)ref.data(),bytes);if(memcmp(ref.data(),warm.data(),bytes))throw std::runtime_error("warm vs15399gold mismatch");
 ID3D12QueryHeap*heap{};D3D12_QUERY_HEAP_DESC hd{};hd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;hd.Count=2;ck(d->CreateQueryHeap(&hd,IID_PPV_ARGS(&heap)));ID3D12Resource*ticks=buffer(d,16,D3D12_HEAP_TYPE_READBACK);UINT64 freq{};ck(q->GetTimestampFrequency(&freq));
 auto measure=[&](bool batch){calls=0;auto start=std::chrono::steady_clock::now();submit.Submit([&](ID3D12GraphicsCommandList*c){c->EndQuery(heap,D3D12_QUERY_TYPE_TIMESTAMP,0);});run4(batch);submit.Submit([&](ID3D12GraphicsCommandList*c){c->EndQuery(heap,D3D12_QUERY_TYPE_TIMESTAMP,1);c->ResolveQueryData(heap,D3D12_QUERY_TYPE_TIMESTAMP,0,2,ticks,0);});submit.Flush();double wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();UINT64 stamp[2];D3D12_RANGE range{0,16};ck(ticks->Map(0,&range,&p));memcpy(stamp,p,16);ticks->Unmap(0,&none);if(stamp[1]<stamp[0]||!freq||calls!=4)throw std::runtime_error("timestamp/counter gate");return std::pair<double,double>{wall,double(stamp[1]-stamp[0])*1000/freq};};
 auto separate=measure(false);auto sa=read(L"timed-separate-check");if(memcmp(ref.data(),sa.data(),bytes))throw std::runtime_error("timedseparate gold mismatch");auto batch=measure(true);auto ba=read(L"timed-batch-check");if(memcmp(ref.data(),ba.data(),bytes))throw std::runtime_error("timedbatch gold mismatch");
 printf("ONE_PAIR_DIAGNOSTIC separate_cpu_wall_ms=%.9g separate_gpu_outer_ms=%.9g batch_cpu_wall_ms=%.9g batch_gpu_outer_ms=%.9g pervariant_stage_record_calls=4 symmetric_extra_marker_lists=2 raw_checks_outside_timing=1 no_formal_performance_claim=1\n",separate.first,separate.second,batch.first,batch.second);submit.Flush();ticks->Release();heap->Release();
 }
submit.Flush();}
 rb->Release();in->Release();}
 q->Release();d->Release();return 0;
 }catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
