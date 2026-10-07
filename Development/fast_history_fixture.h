#pragma once
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <cmath>
#include "../src/native_fast_history_support.h"
using Microsoft::WRL::ComPtr;
using NativeFastHistorySupport::Check;
constexpr auto ReadState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
static void Require(bool condition,const char *message) {if(!condition) throw std::runtime_error(message);}

struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info;
    HANDLE event{}; UINT64 value=0;
    explicit Gpu(bool hardware=false)
    {
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        if(hardware) {
            for(UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> adapter;
                if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
                DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc),"adapter description");
                if(desc.VendorId==0x1002 && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) &&
                   SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))break;
            }
            Require(device!=nullptr,"AMD hardware device");
        } else {
            ComPtr<IDXGIAdapter> warp; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
            Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        }
        device.As(&info);
        if(!debug)std::puts("D3D12 debug layer unavailable; numerical/synchronization tests still execute");
        D3D12_COMMAND_QUEUE_DESC q{}; Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        Check(list->Close(),"initial close");
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); Require(event!=nullptr,"event");
    }
    ~Gpu(){CloseHandle(event);}
    template<class F> void Run(F fn)
    {
        Check(allocator->Reset(),"allocator reset"); Check(list->Reset(allocator.Get(),nullptr),"list reset");
        fn(list.Get()); Check(list->Close(),"close");
        ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
        Check(queue->Signal(fence.Get(),++value),"signal");
        Check(fence->SetEventOnCompletion(value,event),"arm event");
        Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0 && fence->GetCompletedValue()>=value,"GPU completion");
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE heap=D3D12_HEAP_TYPE_DEFAULT)
    {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=heap;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;
        d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(heap==D3D12_HEAP_TYPE_DEFAULT)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto state=heap==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:heap==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:ReadState;
        ComPtr<ID3D12Resource> r;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
    }
    ComPtr<ID3D12Resource> Texture(UINT w,UINT h,DXGI_FORMAT format,UINT16 mips=1,
                                 D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE)
    {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
        d.DepthOrArraySize=1;d.MipLevels=mips;d.SampleDesc.Count=1;d.Format=format;d.Flags=flags;
        ComPtr<ID3D12Resource> r;Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,ReadState,nullptr,IID_PPV_ARGS(&r)),"texture");return r;
    }
    void ClearDepth(ID3D12Resource *r,DXGI_FORMAT format,float value)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"depth heap");
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(r,&view,cpu);
        Run([&](auto *cmd){
            NativeFastHistorySupport::Transition(cmd,r,ReadState,D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cmd->ClearDepthStencilView(cpu,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,value,173,0,nullptr);
            NativeFastHistorySupport::Transition(cmd,r,D3D12_RESOURCE_STATE_DEPTH_WRITE,ReadState);
        });
    }
    void Upload(ID3D12Resource *r,const std::vector<float> &data)
    {
        auto d=r->GetDesc(); const bool texture=d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        UINT64 bytes=data.size()*sizeof(float),rowBytes=bytes;UINT rows=1;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        if(texture)device->GetCopyableFootprints(&d,0,1,0,&fp,&rows,&rowBytes,&bytes);
        Require(data.size()*sizeof(float)>=rowBytes*rows,"upload source size");
        auto up=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);
        void *ptr=nullptr;Check(up->Map(0,nullptr,&ptr),"upload map");
        for(UINT y=0;y<rows;++y)std::memcpy(static_cast<char*>(ptr)+y*(texture?fp.Footprint.RowPitch:rowBytes),
                                         reinterpret_cast<const char*>(data.data())+y*rowBytes,size_t(rowBytes));
        up->Unmap(0,nullptr);
        Run([&](auto *cmd){
            NativeFastHistorySupport::Transition(cmd,r,ReadState,D3D12_RESOURCE_STATE_COPY_DEST);
            if(texture){D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=r;src.pResource=up.Get();
                src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
            else cmd->CopyBufferRegion(r,0,up.Get(),0,bytes);
            NativeFastHistorySupport::Transition(cmd,r,D3D12_RESOURCE_STATE_COPY_DEST,ReadState);
        });
    }
    std::vector<float> Read(ID3D12Resource *r)
    {
        auto desc=r->GetDesc();const bool texture=desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        UINT64 bytes=desc.Width,rowBytes=bytes;UINT rows=1;D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        if(texture)device->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,&rowBytes,&bytes);
        auto rb=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        Run([&](auto *cmd){NativeFastHistorySupport::Transition(cmd,r,ReadState,D3D12_RESOURCE_STATE_COPY_SOURCE);
            if(texture){D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=r;dst.pResource=rb.Get();
                dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
            else cmd->CopyBufferRegion(rb.Get(),0,r,0,bytes);
            NativeFastHistorySupport::Transition(cmd,r,D3D12_RESOURCE_STATE_COPY_SOURCE,ReadState);});
        std::vector<float> data(size_t(rowBytes*rows/4));void *ptr=nullptr;Check(rb->Map(0,nullptr,&ptr),"read map");
        for(UINT y=0;y<rows;++y)std::memcpy(reinterpret_cast<char*>(data.data())+y*rowBytes,
            static_cast<char*>(ptr)+y*(texture?fp.Footprint.RowPitch:rowBytes),size_t(rowBytes));
        rb->Unmap(0,nullptr);return data;
    }
    void NoErrors()
    {
        if(!info)return;
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);
            std::vector<char> buf(bytes);auto *m=reinterpret_cast<D3D12_MESSAGE*>(buf.data());info->GetMessage(i,m,&bytes);
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D12: %s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    }
};
