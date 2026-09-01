#include <cuda.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
    if (argc != 14) {
        std::fprintf(stderr, "usage: %s cubin weights input main-out aux-out symbol width height grid-x grid-y block-y mode shift\nmode: 0=plain 1=inpview 2=1h-ds 3=multihead-ds\n", argv[0]);
        return 2;
    }
    auto weights = read_file(argv[2]), input = read_file(argv[3]);
    int width=std::atoi(argv[7]),height=std::atoi(argv[8]),gx=std::atoi(argv[9]),gy=std::atoi(argv[10]),by=std::atoi(argv[11]),mode=std::atoi(argv[12]),shift=std::atoi(argv[13]);
    constexpr size_t arena_size=8*1024*1024; std::vector<unsigned char> host(arena_size);
    check("init",cuInit(0)); CUdevice device; check("device",cuDeviceGet(&device,0)); CUcontext context; check("context",cuDevicePrimaryCtxRetain(&context,device)); check("current",cuCtxSetCurrent(context));
    CUmodule module; check("module",cuModuleLoad(&module,argv[1])); CUfunction function; check("function",cuModuleGetFunction(&function,module,argv[6]));
    CUdeviceptr di,doo,dw,aux; check("input alloc",cuMemAlloc(&di,arena_size)); check("output alloc",cuMemAlloc(&doo,arena_size)); check("weight alloc",cuMemAlloc(&dw,(weights.size()+65535)&~65535ull)); check("aux alloc",cuMemAlloc(&aux,arena_size));
    check("zero input",cuMemsetD8(di,0,arena_size)); check("zero output",cuMemsetD8(doo,0,arena_size)); check("zero aux",cuMemsetD8(aux,0,arena_size)); check("input",cuMemcpyHtoD(di,input.data(),input.size())); check("weights",cuMemcpyHtoD(dw,weights.data(),weights.size()));
    Params p{}; p.input=di;p.output=doo;p.weights=dw;p.width=width;p.height=height;p.field_y=shift?-4:height;p.field_x=shift?-4:width;unsigned long long dims=(unsigned)height|((unsigned long long)(unsigned)width<<32);p.optional4=(CUdeviceptr)dims;
    if(mode==2){p.optional3=aux;p.optional_dims=dims;p.optional4=0;p.override_width=width/2;p.override_height=height/2;}
    if(mode==3){p.optional3=aux;p.optional_dims=aux;p.optional4=(CUdeviceptr)dims;p.override_width=width/2;p.override_height=height/2;}
    void *args[]={&p}; check("launch",cuLaunchKernel(function,gx,gy,1,32,by,1,0,nullptr,args,nullptr)); check("sync",cuCtxSynchronize());
    for(auto item:{std::pair<const char*,CUdeviceptr>{argv[4],doo},{argv[5],aux}}){check("read",cuMemcpyDtoH(host.data(),item.second,arena_size));write_file(item.first,host.data(),host.size());size_t nz=0,last=0;for(size_t i=0;i<host.size();i++)if(host[i]){nz++;last=i;}std::printf("%s nonzero=%zu last=%zu\n",item.first,nz,last);}
}
