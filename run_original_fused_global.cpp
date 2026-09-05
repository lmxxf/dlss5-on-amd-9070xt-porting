#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>

struct Params {
    CUdeviceptr input, output, weights;
    int width, height, field_y, field_x;
    CUdeviceptr optional0, optional1, optional2, optional3;
    unsigned long long optional_dims;
    CUdeviceptr optional4;
    int override_width, override_height;
};
static_assert(sizeof(Params) == 0x60);

static void check(const char *name, CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char *text = nullptr; cuGetErrorString(result, &text);
        std::fprintf(stderr, "%s=%d %s\n", name, result, text ? text : "?");
        std::exit(1);
    }
}
static std::vector<unsigned char> read_file(const char *path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) std::exit(2); size_t size = (size_t)file.tellg(); file.seekg(0);
    std::vector<unsigned char> bytes(size); file.read((char *)bytes.data(), size); return bytes;
}
static void write_file(const char *path, const void *data, size_t size) {
    std::ofstream(path, std::ios::binary).write((const char *)data, size);
}

int main(int argc, char **argv) {
    if (argc < 14 || argc > 18) {
        std::fprintf(stderr, "usage: %s cubin weights input main-out aux-out symbol width height grid-x grid-y block-y mode shift [optional2-input] [weight-offset] [input-offset] [output-offset]\nlegacy modes: 0=plain 1=inpview 2=1h-ds 3=multihead-ds\nnative modes: 4=C32-plain 5=C32-inpview 6=C32-ds; shift bit0=X bit1=Y\n", argv[0]);
        return 2;
    }
    auto weights = read_file(argv[2]), input = read_file(argv[3]);
    int width=std::atoi(argv[7]),height=std::atoi(argv[8]),gx=std::atoi(argv[9]),gy=std::atoi(argv[10]),by=std::atoi(argv[11]),mode=std::atoi(argv[12]),shift=std::atoi(argv[13]);
    const size_t arena_size=std::max<size_t>(8*1024*1024,(input.size()+65535)&~65535ull); std::vector<unsigned char> host(arena_size);
    check("init",cuInit(0)); CUdevice device; check("device",cuDeviceGet(&device,0)); CUcontext context; check("context",cuDevicePrimaryCtxRetain(&context,device)); check("current",cuCtxSetCurrent(context));
    CUmodule module; check("module",cuModuleLoad(&module,argv[1])); CUfunction function; check("function",cuModuleGetFunction(&function,module,argv[6]));
    CUdeviceptr di,doo,dw,aux; check("input alloc",cuMemAlloc(&di,arena_size)); check("output alloc",cuMemAlloc(&doo,arena_size)); check("weight alloc",cuMemAlloc(&dw,(weights.size()+65535)&~65535ull)); check("aux alloc",cuMemAlloc(&aux,arena_size));
    check("zero input",cuMemsetD8(di,0,arena_size)); check("zero output",cuMemsetD8(doo,0,arena_size)); check("zero aux",cuMemsetD8(aux,0,arena_size)); check("input",cuMemcpyHtoD(di,input.data(),input.size())); check("weights",cuMemcpyHtoD(dw,weights.data(),weights.size()));
    const size_t weight_offset=argc>=16?std::strtoull(argv[15],nullptr,0):0;
    const size_t input_offset=argc>=17?std::strtoull(argv[16],nullptr,0):0;
    const size_t output_offset=argc>=18?std::strtoull(argv[17],nullptr,0):0;
    Params p{}; p.input=di+input_offset;p.output=doo+output_offset;p.weights=dw+weight_offset;p.width=width;p.height=height;p.field_y=shift?-4:height;p.field_x=shift?-4:width;unsigned long long dims=(unsigned)height|((unsigned long long)(unsigned)width<<32);p.optional4=(CUdeviceptr)dims;
    // Native C32 ABI uses height,width then x/y window offsets, not extents.
    if(mode==4){p.width=height;p.height=width;p.field_y=(shift&1)?-4:0;p.field_x=(shift&2)?-4:0;}
    if(mode==5){p.width=height;p.height=width;p.field_y=0;p.field_x=0;p.optional2=aux;}
    if(mode==6){p.width=height;p.height=width;p.field_y=(shift&1)?-4:0;p.field_x=(shift&2)?-4:0;p.optional3=aux;p.optional_dims=(unsigned)(height/2)|((unsigned long long)(unsigned)(width/2)<<32);p.optional4=0;}
    // Multihead ABI inserts an unused pointer before H/W and X/Y offsets.
    if(mode==7||mode==8){p.width=0;p.height=0;p.field_y=height;p.field_x=width;p.optional0=(unsigned)((shift&1)?-4:0)|((unsigned long long)(unsigned)((shift&2)?-4:0)<<32);p.optional4=0;}
    if(mode==8){p.optional_dims=aux;p.optional4=(unsigned)(height/2)|((unsigned long long)(unsigned)(width/2)<<32);}
    if(mode==1){p.field_y=0;p.field_x=0;p.optional2=aux;if(argc>=15&&std::string(argv[14])!="-"){auto optional2=read_file(argv[14]);check("optional2",cuMemcpyHtoD(aux,optional2.data(),std::min(optional2.size(),arena_size)));}}
    if(mode==2){p.optional3=aux;p.optional_dims=dims;p.optional4=0;p.override_width=width/2;p.override_height=height/2;}
    if(mode==3){p.optional3=aux;p.optional_dims=aux;p.optional4=(CUdeviceptr)dims;p.override_width=width/2;p.override_height=height/2;}
    void *args[]={&p};
    if(const char* scan_count=std::getenv("DLSS5_NATIVE_SCAN_COUNT")){
        const char* scan_offset=std::getenv("DLSS5_NATIVE_SCAN_OFFSET");
        size_t count=std::strtoull(scan_count,nullptr,0),begin=scan_offset?std::strtoull(scan_offset,nullptr,0):0;
        const size_t element=std::getenv("DLSS5_NATIVE_SCAN_HALF")?2:1;
        if(mode!=7||width!=8||height!=8||(by!=2&&by!=4)||count==0||count>65536||begin>weights.size()||count>(weights.size()-begin)/element||weight_offset||input_offset||output_offset)return 2;
        if(!std::all_of(weights.begin()+begin,weights.begin()+begin+count*element,[](unsigned char x){return x==0;}))return 2;
        const size_t span=8*8*32*by;std::vector<unsigned char> results(count*span);unsigned short one=element==2?0x4800:0x38,zero=0;
        for(size_t i=0;i<count;i++){
            if(i)check("reset scanned element",cuMemcpyHtoD(dw+begin+(i-1)*element,&zero,element));
            check("set scanned element",cuMemcpyHtoD(dw+begin+i*element,&one,element));check("clear scanned output",cuMemsetD8(doo,0,span));
            check("scan launch",cuLaunchKernel(function,gx,gy,1,32,by,1,0,nullptr,args,nullptr));check("scan sync",cuCtxSynchronize());
            check("scan read",cuMemcpyDtoH(results.data()+i*span,doo,span));
        }
        write_file(argv[4],results.data(),results.size());std::printf("C%d scan offset=%zu count=%zu element_bytes=%zu sample_bytes=%zu\n",32*by,begin,count,element,span);return 0;
    }
    check("launch",cuLaunchKernel(function,gx,gy,1,32,by,1,0,nullptr,args,nullptr)); check("sync",cuCtxSynchronize());
    for(auto item:{std::pair<const char*,CUdeviceptr>{argv[4],doo},{argv[5],aux}}){check("read",cuMemcpyDtoH(host.data(),item.second,arena_size));write_file(item.first,host.data(),host.size());size_t nz=0,last=0;for(size_t i=0;i<host.size();i++)if(host[i]){nz++;last=i;}std::printf("%s nonzero=%zu last=%zu\n",item.first,nz,last);}
}
