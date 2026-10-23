#include "/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/candidate-source/src/native_network_geometry.h"
#include <cassert>
int main(){
 NativeSmallGeometryEnabled()=true;
 struct Case{const char*name;unsigned iw,ih;const char*tier,*rows,*free;unsigned pw,ph,n;};
 Case cases[]={{"Small900",1296,720,"900","1152","0",1600,1024,448},{"Small720",1296,720,"720","1152","0",1344,768,288},{"Small1080_rows1088_is_still1152",1296,720,"1080","1088","0",1920,1152,640},{"Small1080_control",1296,720,"1080","1152","0",1920,1152,640},{"Small_free1072",1920,1072,"1080","1088","1",1920,1088,640}};
 puts("[");bool comma=false;
 for(auto c:cases){setenv("DLSS5_NETWORK_HEIGHT",c.tier,1);setenv("DLSS5_NETWORK_1080_ROWS",c.rows,1);setenv("DLSS5_NETWORK_FREE_RES",c.free,1);NativeNetworkGeometryResolved()=false;auto g=NativeResolveNetworkGeometry(c.iw,c.ih);assert(g.processing_width==c.pw&&g.processing_height==c.ph&&g.VitTokens()==c.n&&g.small_model);
 unsigned dw=NativeNetworkGeometry::Round4((g.processing_width+31)/32),dh=NativeNetworkGeometry::Round4((g.processing_height+31)/32);if(comma)puts(",");comma=true;
 printf("{\"name\":\"%s\",\"input\":[%u,%u],\"valid\":[%u,%u],\"processing\":[%u,%u],\"deep\":[%u,%u],\"vit\":[%u,%u],\"tokens\":%u,\"free\":%u}",c.name,c.iw,c.ih,g.valid_width,g.valid_height,g.processing_width,g.processing_height,dw,dh,g.vit_w,g.vit_h,g.VitTokens(),unsigned(g.Free()));
 }
 puts("\n]");
}
