#pragma once
class Fp16ToFp32RuntimePass {
public:
    void Create(ID3D12Device *device, ID3D12Resource *input, ID3D12Resource *output, UINT values) {
        values_ = values;
        const char shader[] = R"(ByteAddressBuffer input:register(t0);RWStructuredBuffer<float> output:register(u0);float H(uint i){uint x=input.Load((i&~1)*2);return f16tof32((x>>((i&1)*16))&65535);}[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){uint i=id.x+id.y*4194240;if(i<N)output[i]=H(i);})";
        char count[16];std::snprintf(count,sizeof(count),"%u",values);D3D_SHADER_MACRO macros[]={{"N",count},{nullptr,nullptr}};
        ID3DBlob *code=nullptr,*error=nullptr;dmlrt_check("bridge compile",D3DCompile(shader,sizeof(shader)-1,nullptr,macros,nullptr,"main","cs_5_1",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC root_desc{};root_desc.NumParameters=1;root_desc.pParameters=&parameter;ID3DBlob *signature=nullptr;dmlrt_check("bridge signature",D3D12SerializeRootSignature(&root_desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error));dmlrt_check("bridge root",device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)));D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root_;pipeline.CS={code->GetBufferPointer(),code->GetBufferSize()};dmlrt_check("bridge pso",device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&pso_)));
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};dmlrt_check("bridge heap",device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&heap_)));UINT step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Format=DXGI_FORMAT_R32_TYPELESS;srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;srv.Buffer.NumElements=(values+1)/2;device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=step;D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.StructureByteStride=4;uav.Buffer.NumElements=values;device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
    }
    void Record(ID3D12GraphicsCommandList *commands) {
        ID3D12DescriptorHeap *heaps[]={heap_};commands->SetDescriptorHeaps(1,heaps);commands->SetComputeRootSignature(root_);commands->SetComputeRootDescriptorTable(0,heap_->GetGPUDescriptorHandleForHeapStart());commands->SetPipelineState(pso_);UINT64 groups=(UINT64(values_)+63)/64;commands->Dispatch((UINT)std::min<UINT64>(groups,65535),(UINT)((groups+65534)/65535),1);
    }
private:
    UINT values_{};ID3D12RootSignature *root_{};ID3D12PipelineState *pso_{};ID3D12DescriptorHeap *heap_{};
};
