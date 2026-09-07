#include "native_submission_order_probe.cpp"
static const Header*seen{};static unsigned calls{};
static uint32_t fake(void**,const Header*h){seen=h;++calls;return 12345;}
int main(){
 original=fake;struct Payload{Header header;void*list;}p{{0x00010001,nullptr},reinterpret_cast<void*>(0x1234)};
 if(dispatch(nullptr,&p.header)!=12345||seen!=&p.header||calls!=1||frames!=1)return 1;
 if(p.header.type!=0x00010001||p.header.next||p.list!=reinterpret_cast<void*>(0x1234))return 2;
 if(dispatch(nullptr,nullptr)!=12345||seen||calls!=2||frames!=1)return 3;
 if(compute(nullptr,1,1,1)||draw(nullptr,3,1,0,0))return 4;
 puts("submission observer forwards original result/payload; events do not suppress work; live ordering not proven");return 0;
}
