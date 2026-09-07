#include "native_submission_order_probe.cpp"
static const Header*seen{};static unsigned calls{};
static uint32_t fake(void**,const Header*h){seen=h;++calls;return 12345;}
static unsigned execute_calls{};static ID3D12CommandList*const*seen_lists{};static UINT seen_count{};
static void STDMETHODCALLTYPE fake_execute(ID3D12CommandQueue*,UINT n,ID3D12CommandList*const*l){++execute_calls;seen_count=n;seen_lists=l;}
static const D3D12_RESOURCE_BARRIER*seen_barriers{};static UINT seen_barrier_count{};
static void STDMETHODCALLTYPE fake_barriers(ID3D12GraphicsCommandList*,UINT n,const D3D12_RESOURCE_BARRIER*b){seen_barriers=b;seen_barrier_count=n;}
int main(){
 original=fake;struct Payload{Header header;void*list;unsigned char padding[288];ResourcePayload output;}p{};p.header.type=0x00010001;p.list=reinterpret_cast<void*>(0x1234);
 if(dispatch(nullptr,&p.header)!=12345||seen!=&p.header||calls!=1||frames!=1)return 1;
 if(p.header.type!=0x00010001||p.header.next||p.list!=reinterpret_cast<void*>(0x1234))return 2;
 if(dispatch(nullptr,nullptr)!=12345||seen||calls!=2||frames!=1)return 3;
 if(compute(nullptr,1,1,1)||draw(nullptr,3,1,0,0))return 4;
 tracked_output=0x123;reshade::api::resource r{0x456};reshade::api::resource_usage before=reshade::api::resource_usage::unordered_access,after=reshade::api::resource_usage::shader_resource;
 auto previous=events.load();barrier(nullptr,1,&r,&before,&after);if(events.load()!=previous)return 5;
 r.handle=0x123;barrier(nullptr,1,&r,&before,&after);if(events.load()!=previous+1||r.handle!=0x123||before!=reshade::api::resource_usage::unordered_access||after!=reshade::api::resource_usage::shader_resource)return 6;
 original_execute=fake_execute;ID3D12CommandList*list=reinterpret_cast<ID3D12CommandList*>(0x5678);execute_native(nullptr,1,&list);
 if(execute_calls!=1||seen_count!=1||seen_lists!=&list||list!=reinterpret_cast<ID3D12CommandList*>(0x5678))return 7;
 original_barriers=fake_barriers;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={reinterpret_cast<ID3D12Resource*>(0x123),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
 native_barriers(nullptr,1,&b);if(seen_barriers!=&b||seen_barrier_count!=1||b.Transition.StateBefore!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS||b.Transition.StateAfter!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)return 8;
 puts("submission observer forwards original result/payload; events do not suppress work; live ordering not proven");return 0;
}
