#pragma once
#include <DirectML.h>
#include <d3d12.h>
#include <algorithm>
#include <cstdio>

inline void dmlrt_check(const char* name, HRESULT result) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s=0x%08lx\n", name, (unsigned long)result);
        ExitProcess(1);
    }
}

inline const GUID DMLRT_IID_OPERATOR = {0x26caae7a,0x3081,0x4633,{0x95,0x81,0x22,0x6f,0xbe,0x57,0x69,0x5d}};
inline const GUID DMLRT_IID_COMPILED = {0x6b15e56a,0xbf5c,0x4902,{0x92,0xd8,0xda,0x3a,0x65,0x0a,0xfe,0xa4}};
inline const GUID DMLRT_IID_INITIALIZER = {0x427c1113,0x435c,0x469c,{0x86,0x76,0x4d,0x5d,0xd0,0x72,0xf8,0x13}};
inline const GUID DMLRT_IID_TABLE = {0x29c687dc,0xde74,0x4e3b,{0xab,0x00,0x11,0x68,0xf2,0xfc,0x3c,0xfc}};

inline DML_BINDING_PROPERTIES dmlrt_binding_properties(IDMLDispatchable* dispatchable) {
    DML_BINDING_PROPERTIES value{};
    using Function = void (WINAPI*)(IDMLDispatchable*, DML_BINDING_PROPERTIES*);
    ((Function)(*(void***)dispatchable)[8])(dispatchable, &value);
    return value;
}

class DmlGemmOperator {
public:
    void Create(IDMLDevice* dml, ID3D12Device* dx, UINT batch, UINT m, UINT k, UINT n) {
        batch_ = batch; m_ = m; k_ = k; n_ = n;
        UINT aSizes[4] = {batch, 1, m, k};
        UINT bSizes[4] = {batch, 1, k, n};
        UINT oSizes[4] = {batch, 1, m, n};
        DML_BUFFER_TENSOR_DESC aBuffer{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,aSizes,nullptr,UINT64(batch)*m*k*2,0};
        DML_BUFFER_TENSOR_DESC bBuffer{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,bSizes,nullptr,UINT64(batch)*k*n*2,0};
        DML_BUFFER_TENSOR_DESC oBuffer{DML_TENSOR_DATA_TYPE_FLOAT16,DML_TENSOR_FLAG_NONE,4,oSizes,nullptr,UINT64(batch)*m*n*2,0};
        DML_TENSOR_DESC a{DML_TENSOR_TYPE_BUFFER,&aBuffer}, b{DML_TENSOR_TYPE_BUFFER,&bBuffer}, o{DML_TENSOR_TYPE_BUFFER,&oBuffer};
        DML_GEMM_OPERATOR_DESC gemm{&a,&b,nullptr,&o,DML_MATRIX_TRANSFORM_NONE,DML_MATRIX_TRANSFORM_NONE,1.0f,0.0f,nullptr};
        DML_OPERATOR_DESC desc{DML_OPERATOR_GEMM,&gemm};
        IDMLOperator* raw = nullptr;
        dmlrt_check("CreateOperator", dml->CreateOperator(&desc, DMLRT_IID_OPERATOR, (void**)&raw));
        dmlrt_check("CompileOperator", dml->CompileOperator(raw, DML_EXECUTION_FLAG_ALLOW_HALF_PRECISION_COMPUTATION, DMLRT_IID_COMPILED, (void**)&compiled_));
        IDMLCompiledOperator* operators[] = {compiled_};
        dmlrt_check("CreateOperatorInitializer", dml->CreateOperatorInitializer(1, operators, DMLRT_IID_INITIALIZER, (void**)&initializer_));
        auto init = dmlrt_binding_properties(initializer_), exec = dmlrt_binding_properties(compiled_);
        if (init.TemporaryResourceSize || exec.TemporaryResourceSize || exec.PersistentResourceSize) {
            std::fprintf(stderr, "unexpected GEMM scratch for %ux%ux%u batch %u\n", m, k, n, batch);
            ExitProcess(2);
        }
        descriptorCount_ = std::max<UINT>(1, std::max(init.RequiredDescriptorCount, exec.RequiredDescriptorCount));
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,descriptorCount_,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        dmlrt_check("CreateDescriptorHeap", dx->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_)));
        DML_BINDING_TABLE_DESC tableDesc{initializer_,heap_->GetCPUDescriptorHandleForHeapStart(),heap_->GetGPUDescriptorHandleForHeapStart(),descriptorCount_};
        dmlrt_check("CreateBindingTable", dml->CreateBindingTable(&tableDesc, DMLRT_IID_TABLE, (void**)&table_));
    }
    void RecordInitialization(IDMLCommandRecorder* recorder, ID3D12GraphicsCommandList* commands) {
        ID3D12DescriptorHeap* heaps[] = {heap_};
        commands->SetDescriptorHeaps(1, heaps);
        recorder->RecordDispatch(commands, initializer_, table_);
    }
    void Bind(ID3D12Resource* a, ID3D12Resource* b, ID3D12Resource* output) {
        DML_BINDING_TABLE_DESC tableDesc{compiled_,heap_->GetCPUDescriptorHandleForHeapStart(),heap_->GetGPUDescriptorHandleForHeapStart(),descriptorCount_};
        dmlrt_check("ResetBindingTable", table_->Reset(&tableDesc));
        DML_BUFFER_BINDING buffers[2] = {{a,0,ABytes()},{b,0,BBytes()}}, outBuffer{output,0,OutputBytes()};
        DML_BINDING_DESC inputs[3] = {{DML_BINDING_TYPE_BUFFER,&buffers[0]},{DML_BINDING_TYPE_BUFFER,&buffers[1]},{DML_BINDING_TYPE_NONE,nullptr}};
        DML_BINDING_DESC out{DML_BINDING_TYPE_BUFFER,&outBuffer};
        table_->BindInputs(3, inputs); table_->BindOutputs(1, &out);
    }
    void Record(IDMLCommandRecorder* recorder, ID3D12GraphicsCommandList* commands) {
        ID3D12DescriptorHeap* heaps[] = {heap_};
        commands->SetDescriptorHeaps(1, heaps);
        recorder->RecordDispatch(commands, compiled_, table_);
    }
    UINT64 ABytes() const { return UINT64(batch_)*m_*k_*2; }
    UINT64 BBytes() const { return UINT64(batch_)*k_*n_*2; }
    UINT64 OutputBytes() const { return UINT64(batch_)*m_*n_*2; }
private:
    UINT batch_{},m_{},k_{},n_{},descriptorCount_{};
    IDMLCompiledOperator* compiled_{};
    IDMLOperatorInitializer* initializer_{};
    IDMLBindingTable* table_{};
    ID3D12DescriptorHeap* heap_{};
};
