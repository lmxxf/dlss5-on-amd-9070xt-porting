#include "hip_reference_network.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
using hip_probe::Api;using hip_probe::Handle;
constexpr size_t GUARD=256;
struct Buffer{Api&a;void*p=nullptr;size_t size;Buffer(Api&api,const std::vector<uint8_t>&v):a(api),size(v.size()){a.Check(a.hipMalloc(&p,size),"alloc");try{Write(v);}catch(...){a.hipFree(p);p=nullptr;throw;}}~Buffer(){if(p)a.hipFree(p);}Buffer(const Buffer&)=delete;void Write(const std::vector<uint8_t>&v){if(v.size()!=size)throw std::runtime_error("upload size");a.Check(a.hipMemcpy(p,v.data(),size,1),"upload");}std::vector<uint8_t>Read(){std::vector<uint8_t>v(size);a.Check(a.hipMemcpy(v.data(),p,size,2),"readback");return v;}void*At(size_t n){return static_cast<uint8_t*>(p)+n;}};
static std::vector<uint8_t> Guarded(size_t payload,uint8_t fill){std::vector<uint8_t>v(payload+GUARD*2,0xa5);std::fill(v.begin()+GUARD,v.end()-GUARD,fill);return v;}
static size_t BadGuard(const std::vector<uint8_t>&v){size_t n=0;for(size_t i=0;i<GUARD;i++)n+=v[i]!=0xa5,n+=v[v.size()-GUARD+i]!=0xa5;return n;}
static void Save(const std::filesystem::path&p,const std::vector<uint8_t>&v){std::ofstream f(p,std::ios::binary);if(!f||!f.write(reinterpret_cast<const char*>(v.data()),v.size()))throw std::runtime_error("write raw");}
static std::string Escape(const std::string&s){std::string o;for(unsigned char c:s){if(c=='"'||c==char(92)){o+=char(92);o+=char(c);}else if(c<32)o+=' ';else o+=char(c);}return o;}
static std::vector<uint8_t> PackWeights(const std::filesystem::path&p,std::filesystem::path&resolved,size_t&source_bytes,size_t&expanded_bytes){
 constexpr size_t elements=3*1048576+32;const std::string logical=p.u8string();if(p.extension()!=L".f32")throw std::runtime_error("pass logical block31-qkv.f32; production loader resolves .f16 fallback");
 std::ifstream exists(p,std::ios::binary);const bool full=bool(exists);exists.close();resolved=full?p:std::filesystem::u8path(logical.substr(0,logical.size()-4)+".f16");
 auto v=hip_reference::ReadWeights(logical);source_bytes=std::filesystem::file_size(resolved);expanded_bytes=v.size()*sizeof(float);if(v.size()!=elements)throw std::runtime_error("expanded QKV weights require 3*1048576+32 floats");
 // Exact production PackedVitQkvWeightFrag8 loop. No weight quantization approximation.
 std::vector<uint8_t>t(size_t(3)*1048576+32*4);
 for(size_t part=0;part<3;part++)for(size_t nt=0;nt<64;nt++)for(size_t kt=0;kt<64;kt++)for(unsigned g=0;g<2;g++)for(unsigned rc=0;rc<16;rc++)for(unsigned e=0;e<8;e++)t[part*1048576+(nt*64+kt)*256+(g*16+rc)*8+e]=hip_reference::ExactWeightFp8(v[part*1048576+(nt*16+rc)*1024+kt*16+g*8+e]);
 for(size_t i=3*1048576;i<v.size();++i)if(!std::isfinite(v[i]))throw std::runtime_error("nonfinite QKV scale");std::memcpy(t.data()+3*1048576,v.data()+3*1048576,32*4);return t;
}
static Handle Fn(Api&a,Handle m,const char*n){Handle f=nullptr;a.Check(a.hipModuleGetFunction(&f,m,n),n);if(!f)throw std::runtime_error("null kernel");return f;}
static void Launch(Api&a,Handle stream,Handle f,unsigned groups,unsigned threads,void**args){int rc=a.hipModuleLaunchKernel(f,groups,1,1,threads,1,1,0,stream,args,nullptr);int drain=a.hipStreamSynchronize(stream);if(drain){std::fprintf(stderr,"PAIR_PROBE_UNCERTAIN_DRAIN %d\n",drain);std::fflush(stderr);std::_Exit(3);}a.Check(rc,"kernel launch");}
int main(int argc,char**argv){try{
 if(argc!=4&&argc!=5)throw std::runtime_error("usage: pair-probe.exe MODULE_DIRECTORY LOGICAL_BLOCK31_QKV_F32 OUTPUT_DIRECTORY [HIP_DEVICE=0]");
 auto modules=std::filesystem::u8path(argv[1]),weightpath=std::filesystem::u8path(argv[2]),outdir=std::filesystem::u8path(argv[3]);std::filesystem::create_directories(outdir);std::filesystem::path resolved_weight;size_t source_weight_bytes=0,expanded_weight_bytes=0;auto packed=PackWeights(weightpath,resolved_weight,source_weight_bytes,expanded_weight_bytes);
 Api api(7);api.Check(api.hipInit(0),"init");int device=argc==5?std::stoi(argv[4]):0;api.Check(api.hipSetDevice(device),"device");auto props=api.Properties(device);if(std::strncmp(props.gcnArchName,"gfx1201",7))throw std::runtime_error("requires gfx1201");
 Handle stream=nullptr,producer=nullptr,attention=nullptr;api.Check(api.hipStreamCreate(&stream),"stream");
 try{
 api.Check(api.LoadModule(&producer,(modules/"vit-stream-fast.hsaco").u8string().c_str()),"producer module");api.Check(api.LoadModule(&attention,(modules/"deep_fast-packed-fast.hsaco").u8string().c_str()),"attention module");auto pold=Fn(api,producer,"vit_stream_qkv_frag_bin_w5f8"),pnew=Fn(api,producer,"vit_stream_qkv_frag_bin_w5f8_vfrag");
 Buffer weights(api,packed);Buffer gate(api,std::vector<uint8_t>(4,0));bool allpass=true;std::ostringstream receipt;receipt<<"{\n\"packed_weight_bytes\":"<<packed.size()<<",\"source_weight_bytes\":"<<source_weight_bytes<<",\"expanded_weight_bytes\":"<<expanded_weight_bytes<<",\"resolved_weight_path\":\""<<Escape(resolved_weight.u8string())<<"\""<<",\"guard_bytes\":256,\"cases\":[\n";bool comma=false;
 for(unsigned n:{400u,640u}){
  const size_t qkvbytes=size_t(n)*3072,avbytes=size_t(n)*1024;std::vector<uint8_t> input(size_t(n)*1024);constexpr uint8_t small[]={0x08,0x10,0x18,0x20,0x28,0x00,0x88,0x90,0x98,0xa0,0xa8};for(unsigned t=0;t<n;t++)for(unsigned c=0;c<1024;c++)input[size_t(t)*1024+c]=small[(t*131+c*17+(t*c)%29)%11];
  Buffer in(api,input),qo(api,Guarded(qkvbytes,0xb7)),qn(api,Guarded(qkvbytes,0xc9)),ao(api,Guarded(avbytes,0xda)),an(api,Guarded(avbytes,0xeb));
  void*pi=in.p;void*pw=weights.p;void*po=qo.At(GUARD);void*pn=qn.At(GUARD);void*pg=gate.p;void*pa=ao.At(GUARD);void*pb=an.At(GUARD);unsigned tokens=n;
  void* op[]={&pi,&pw,&po,&tokens,&pg};void* np[]={&pi,&pw,&pn,&tokens,&pg};const unsigned producer_groups=96*((n/16+4)/5);Launch(api,stream,pold,producer_groups,160,op);Launch(api,stream,pnew,producer_groups,160,np);
  auto old=qo.Read(),fresh=qn.Read();size_t qkdiff=0,vdiff=0,nanbytes=0;std::vector<uint8_t>inverse(qkvbytes);std::copy(fresh.begin()+GUARD,fresh.begin()+GUARD+2*avbytes,inverse.begin());for(size_t i=0;i<2*avbytes;i++)qkdiff+=old[GUARD+i]!=fresh[GUARD+i];
  for(unsigned t=0;t<n;t++)for(unsigned c=0;c<1024;c++){size_t src=2*avbytes+(size_t(c/32)*(n/16)+t/16)*512+((t%16)/8*16+c%16)*16+(c%32)/16*8+t%8;size_t dst=2*avbytes+size_t(t)*1024+c;inverse[dst]=fresh[GUARD+src];vdiff+=old[GUARD+dst]!=inverse[dst];}
  for(size_t i=0;i<qkvbytes;i++)nanbytes+=(old[GUARD+i]&127)==127,nanbytes+=(inverse[i]&127)==127;
  const auto aold=Fn(api,attention,("vit_attention_fused_"+std::to_string(n)+"_bytein_bout").c_str()),anew=Fn(api,attention,("vit_attention_fused_"+std::to_string(n)+"_bytein_bout_vfrag").c_str());void* oa[]={&po,&pa,&tokens,&pg};void* na[]={&pn,&pb,&tokens,&pg};Launch(api,stream,aold,n*2,32,oa);Launch(api,stream,anew,n*2,32,na);auto avold=ao.Read(),avnew=an.Read();size_t avdiff=0;for(size_t i=0;i<avbytes;i++){avdiff+=avold[GUARD+i]!=avnew[GUARD+i];nanbytes+=(avold[GUARD+i]&127)==127;nanbytes+=(avnew[GUARD+i]&127)==127;}
  size_t guards=BadGuard(old)+BadGuard(fresh)+BadGuard(avold)+BadGuard(avnew);const bool input_same=in.Read()==input,weights_same=weights.Read()==packed;auto dir=outdir/std::to_string(n);std::filesystem::create_directories(dir);Save(dir/"input.e4m3",input);Save(dir/"qkv-old-guard.bin",old);Save(dir/"qkv-new-guard.bin",fresh);Save(dir/"qkv-new-inverse.bin",inverse);Save(dir/"av-old-guard.bin",avold);Save(dir/"av-new-guard.bin",avnew);
  // All four ABIs have a leading reuse-gate early exit, confirmed from actual exports.
  gate.Write(std::vector<uint8_t>{1,0,0,0});auto sqo=Guarded(qkvbytes,0x37),sqn=Guarded(qkvbytes,0x49),sao=Guarded(avbytes,0x5b),san=Guarded(avbytes,0x6d);qo.Write(sqo);qn.Write(sqn);ao.Write(sao);an.Write(san);Launch(api,stream,pold,producer_groups,160,op);Launch(api,stream,pnew,producer_groups,160,np);Launch(api,stream,aold,n*2,32,oa);Launch(api,stream,anew,n*2,32,na);bool reuse=qo.Read()==sqo&&qn.Read()==sqn&&ao.Read()==sao&&an.Read()==san;gate.Write(std::vector<uint8_t>(4,0));
  bool pass=qkdiff==0&&vdiff==0&&avdiff==0&&guards==0&&nanbytes==0&&reuse&&input_same&&weights_same;allpass&=pass;if(comma)receipt<<",\n";comma=true;receipt<<"{\"tokens\":"<<n<<",\"producer_groups\":"<<producer_groups<<",\"producer_threads\":160,\"attention_groups\":"<<n*2<<",\"attention_threads\":32,\"qkv_capacity_bytes_old\":"<<qkvbytes<<",\"qkv_capacity_bytes_new\":"<<qkvbytes<<",\"qk_byte_diff\":"<<qkdiff<<",\"v_inverse_byte_diff\":"<<vdiff<<",\"av_byte_diff\":"<<avdiff<<",\"canary_changed\":"<<guards<<",\"nonfinite_e4m3_bytes\":"<<nanbytes<<",\"reuse_gate_sentinels_unchanged\":"<<(reuse?"true":"false")<<",\"input_unchanged\":"<<(input_same?"true":"false")<<",\"packed_weight_unchanged\":"<<(weights_same?"true":"false")<<",\"pass\":"<<(pass?"true":"false")<<"}";
  std::printf("PAIR_PROBE tokens=%u QKdiff=%zu Vdiff=%zu AVdiff=%zu canary=%zu nan=%zu reuse=%u pass=%u\n",n,qkdiff,vdiff,avdiff,guards,nanbytes,unsigned(reuse),unsigned(pass));
 }
 receipt<<"\n],\"pass\":"<<(allpass?"true":"false")<<"}\n";std::ofstream f(outdir/"receipt.json");f<<receipt.str();f.close();if(!f)throw std::runtime_error("receipt write");std::cout<<receipt.str();api.Check(api.hipModuleUnload(attention),"unload attention");attention=nullptr;api.Check(api.hipModuleUnload(producer),"unload producer");producer=nullptr;api.Check(api.hipStreamDestroy(stream),"destroy stream");stream=nullptr;return allpass?0:1;
 }catch(...){if(stream&&api.hipStreamSynchronize(stream)){std::fflush(stderr);std::_Exit(3);}if(attention)api.hipModuleUnload(attention);if(producer)api.hipModuleUnload(producer);if(stream)api.hipStreamDestroy(stream);throw;}
 }catch(const std::exception&e){std::fprintf(stderr,"PAIR_PROBE_ERROR %s\n",e.what());return 1;}}
