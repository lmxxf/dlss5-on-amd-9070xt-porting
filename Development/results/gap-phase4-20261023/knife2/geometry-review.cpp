#include "/home/lmxxf/work/ai-theorys-study/wechat/assets/297/src/native_network_geometry.h"
#include <cassert>
#include <iostream>
void reset(){setenv("DLSS5_NETWORK_FREE_RES","0",1);setenv("DLSS5_NETWORK_HEIGHT","1080",1);unsetenv("DLSS5_NETWORK_1080_ROWS");unsetenv("DLSS5_NETWORK_FREE_PAD");NativeSmallGeometryEnabled()=true;}
void emit(const char*n,const NativeNetworkGeometry&g){std::cout<<n<<" valid="<<g.valid_width<<"x"<<g.valid_height<<" processing="<<g.processing_width<<"x"<<g.processing_height<<" vit="<<g.vit_w<<"x"<<g.vit_h<<" tokens="<<g.VitTokens()<<" free="<<g.Free()<<" small="<<g.small_model<<"\n";}
int main(){reset();setenv("DLSS5_NETWORK_HEIGHT","900",1);auto a=NativeResolveNetworkGeometry(1600,900);assert(a.processing_width==1600&&a.processing_height==1024&&a.VitTokens()==448);emit("small900",a);
reset();setenv("DLSS5_NETWORK_HEIGHT","720",1);auto b=NativeResolveNetworkGeometry(1280,720);assert(b.processing_width==1344&&b.processing_height==768&&b.VitTokens()==288);emit("small720",b);
reset();setenv("DLSS5_NETWORK_1080_ROWS","1088",1);auto c=NativeResolveNetworkGeometry(1920,1080);assert(c.processing_width==1920&&c.processing_height==1152&&c.VitTokens()==640);emit("small1080-request1088",c);
reset();setenv("DLSS5_NETWORK_FREE_RES","1",1);auto d=NativeResolveNetworkGeometry(1920,1072);assert(d.processing_width==1920&&d.processing_height==1088&&d.VitTokens()==640&&d.Free());emit("small-free1072",d);
}
