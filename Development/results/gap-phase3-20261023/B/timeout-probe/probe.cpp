#include "hip_api.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

using hip_probe::Api;
using hip_probe::Handle;
struct DeviceBuffer {
 Api& api; void* base=nullptr; size_t bytes;
 DeviceBuffer(Api&a,const std::vector<unsigned char>&v):api(a),bytes(v.size()){
  api.Check(api.hipMalloc(&base,bytes),"probe allocation");
  try{api.Check(api.hipMemcpy(base,v.data(),bytes,1),"probe initial upload");}
  catch(...){api.hipFree(base);base=nullptr;throw;}
 }
 ~DeviceBuffer(){if(base)api.hipFree(base);}
 DeviceBuffer(const DeviceBuffer&)=delete;
 std::vector<unsigned char> Read(){std::vector<unsigned char>v(bytes);api.Check(api.hipMemcpy(v.data(),base,bytes,2),"probe readback");return v;}
 void* At(size_t offset){if(offset>=bytes)throw std::runtime_error("buffer offset");return static_cast<unsigned char*>(base)+offset;}
};
static void Save(const std::filesystem::path&p,const std::vector<unsigned char>&v){std::ofstream f(p,std::ios::binary);if(!f||!f.write(reinterpret_cast<const char*>(v.data()),v.size()))throw std::runtime_error("probe output write");}
static std::string Escape(const std::string&s){std::string o;for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=char(c);}else if(c<32)o+=' ';else o+=char(c);}return o;}
struct OutputCheck {size_t nonzero=0,canary_changed=0;};
static OutputCheck CheckOutput(const std::vector<unsigned char>&v,size_t guard,size_t payload,unsigned char canary){OutputCheck r;for(size_t i=0;i<v.size();++i){if(i>=guard&&i<guard+payload)r.nonzero+=v[i]!=0;else r.canary_changed+=v[i]!=canary;}return r;}
int main(int argc,char**argv){
 try{
  if(argc!=3&&argc!=4)throw std::runtime_error("usage: timeout-probe.exe MODULE_HSACO OUTPUT_DIRECTORY [HIP_DEVICE=0]");
  const auto modulepath=std::filesystem::u8path(argv[1]);const auto outdir=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(outdir);
  const int device=argc==4?std::stoi(argv[3]):0;Api api(7);api.Check(api.hipInit(0),"HIP init");int count=0;api.Check(api.hipGetDeviceCount(&count),"device count");if(device<0||device>=count)throw std::runtime_error("device index");api.Check(api.hipSetDevice(device),"select device");auto props=api.Properties(device);
  if(std::strncmp(props.gcnArchName,"gfx1201",7))throw std::runtime_error("probe requires validated gfx1201 target");
  Handle stream=nullptr,module=nullptr;bool enqueued=false;
  api.Check(api.hipStreamCreate(&stream),"create probe stream");
  try{
   const int module_status=api.LoadModule(&module,modulepath.u8string().c_str());api.Check(module_status,"load trace-off consumer module");
   Handle fn=nullptr;const int export_status=api.hipModuleGetFunction(&fn,module,"split_projection_frag_w16_bskip_chain");api.Check(export_status,"lookup chain consumer");if(!fn)throw std::runtime_error("null consumer function");
   constexpr size_t guard=256,n=16,channels=512,f32bytes=n*channels*4,b8bytes=n*channels;
   // Valid, readable poison storage: even an unintended read cannot dereference null.
   const std::vector<unsigned char> input_init(b8bytes,0x5b),skip_init(f32bytes,0x73),weights_init((262144+512)*4,0x7f),trace_init(8*8,0x9b);
   std::vector<unsigned char> f32_init(guard+f32bytes+guard,0xa5),b8_init(guard+b8bytes+guard,0x5a);
   std::fill(f32_init.begin()+guard,f32_init.begin()+guard+f32bytes,0x6d);std::fill(b8_init.begin()+guard,b8_init.begin()+guard+b8bytes,0x7b);
   std::vector<unsigned char> ready_init(guard+8+guard,0xc3),status_init(guard+4+guard,0xd4);
   std::fill(ready_init.begin()+guard,ready_init.begin()+guard+8,0);std::fill(status_init.begin()+guard,status_init.begin()+guard+4,0);
   // Buffers live until the private stream is explicitly drained, including failure paths.
   auto in=std::make_unique<DeviceBuffer>(api,input_init),skip=std::make_unique<DeviceBuffer>(api,skip_init),weights=std::make_unique<DeviceBuffer>(api,weights_init),trace=std::make_unique<DeviceBuffer>(api,trace_init);
   auto of=std::make_unique<DeviceBuffer>(api,f32_init),ob=std::make_unique<DeviceBuffer>(api,b8_init),ready=std::make_unique<DeviceBuffer>(api,ready_init),status=std::make_unique<DeviceBuffer>(api,status_init);
   void* pin=in->base;void* pw=weights->base;void* ps=skip->base;void* pf=of->At(guard);void* pb=ob->At(guard);uint32_t tokens=16;void* pr=ready->At(guard);uint64_t target=8;void* pst=status->At(guard);uint32_t wait_limit=1;void* pt=trace->base;
   void* args[]={&pin,&pw,&ps,&pf,&pb,&tokens,&pr,&target,&pst,&wait_limit,&pt};
   int launch_status=-1,drain_status=-1;auto started=std::chrono::steady_clock::now();
   // No producer, no any-order dependency and no downstream kernel exists in this probe.
   try{launch_status=api.hipModuleLaunchKernel(fn,8,1,1,32,1,1,0,stream,args,nullptr);enqueued=true;api.Check(launch_status,"launch timeout consumer");drain_status=api.hipStreamSynchronize(stream);api.Check(drain_status,"drain timeout consumer");enqueued=false;}
   catch(...){int ec=api.hipStreamSynchronize(stream);enqueued=false;if(ec){std::fprintf(stderr,"TIMEOUT_PROBE_UNCERTAIN_DRAIN result=%d: exit without freeing GPU resources\n",ec);std::fflush(stderr);std::_Exit(3);}throw;}
   const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
   const auto f=of->Read(),b=ob->Read(),r=ready->Read(),s=status->Read();uint32_t status_value=0;std::memcpy(&status_value,s.data()+guard,4);
   auto fc=CheckOutput(f,guard,f32bytes,0xa5),bc=CheckOutput(b,guard,b8bytes,0x5a);size_t status_canary=0;for(size_t i=0;i<s.size();++i)if(i<guard||i>=guard+4)status_canary+=s[i]!=status_init[i];
   const bool inputs_unchanged=in->Read()==input_init&&skip->Read()==skip_init&&weights->Read()==weights_init&&trace->Read()==trace_init;
   const bool ready_unchanged=r==ready_init;
   Save(outdir/"float-output-with-canaries.bin",f);Save(outdir/"byte-output-with-canaries.bin",b);Save(outdir/"ready-with-canaries.bin",r);Save(outdir/"status-with-canaries.bin",s);
   const bool pass=module_status==0&&export_status==0&&launch_status==0&&drain_status==0&&status_value==1&&fc.nonzero==0&&bc.nonzero==0&&fc.canary_changed==0&&bc.canary_changed==0&&status_canary==0&&ready_unchanged&&inputs_unchanged;
   std::ostringstream j;j<<"{\n  \"pass\": "<<(pass?"true":"false")<<",\n  \"module\": \""<<Escape(modulepath.u8string())<<"\",\n  \"device\": "<<device<<",\n  \"device_name\": \""<<Escape(props.name)<<"\",\n  \"architecture\": \""<<Escape(props.gcnArchName)<<"\",\n  \"tokens\": 16, \"workgroups\": 8, \"threads_per_workgroup\": 32,\n  \"ready_initial\": 0, \"target\": 8, \"wait_limit\": 1,\n  \"module_load_status\": "<<module_status<<", \"export_status\": "<<export_status<<", \"launch_status\": "<<launch_status<<", \"drain_status\": "<<drain_status<<",\n  \"sticky_status\": "<<status_value<<",\n  \"float_output_nonzero_bytes\": "<<fc.nonzero<<", \"byte_output_nonzero_bytes\": "<<bc.nonzero<<",\n  \"float_canary_changed\": "<<fc.canary_changed<<", \"byte_canary_changed\": "<<bc.canary_changed<<", \"status_canary_changed\": "<<status_canary<<",\n  \"ready_unchanged\": "<<(ready_unchanged?"true":"false")<<", \"poison_inputs_and_trace_unchanged\": "<<(inputs_unchanged?"true":"false")<<",\n  \"host_launch_and_drain_ms_diagnostic_only\": "<<elapsed<<",\n  \"producer_launches\": 0, \"downstream_launches\": 0\n}\n";
   std::ofstream receipt(outdir/"receipt.json");receipt<<j.str();receipt.close();if(!receipt)throw std::runtime_error("receipt write");std::cout<<j.str();
   // No GPU work remains; destruction order is buffers -> module -> stream.
   status.reset();ready.reset();ob.reset();of.reset();trace.reset();weights.reset();skip.reset();in.reset();api.Check(api.hipModuleUnload(module),"unload module");module=nullptr;api.Check(api.hipStreamDestroy(stream),"destroy stream");stream=nullptr;
   std::cout<<"TIMEOUT_PROBE_"<<(pass?"PASS":"FAIL")<<" outputs_defined=1 canaries_checked=1\n";return pass?0:1;
  }catch(...){if(enqueued&&api.hipStreamSynchronize(stream)){std::fflush(stderr);std::_Exit(3);}if(module)api.hipModuleUnload(module);if(stream)api.hipStreamDestroy(stream);throw;}
 }catch(const std::exception&e){std::fprintf(stderr,"TIMEOUT_PROBE_ERROR %s\n",e.what());return 1;}
}
