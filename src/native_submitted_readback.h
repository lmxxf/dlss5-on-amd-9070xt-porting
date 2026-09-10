#pragma once
#include "native_lab_paths.h"
#include "native_pinned_resource.h"
#include "native_game_submission.h"
#include "native_device_identity.h"
#include <vector>
#include <cstring>
// Diagnostic only. The producer MUST already be submitted to this DIRECT queue.
// Does not modify pixels; transitions the texture back to exactly 'before'.
inline std::vector<unsigned char> NativeReadSubmittedFrame(ID3D12CommandQueue*q,ID3D12Resource*source,D3D12_RESOURCE_STATES before){
 if(!q||!source||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("readback queue/source contract");
 auto desc=source->GetDesc();
 if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=1920||desc.Height!=1080||!NativeIsGameColor(desc.Format)||desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1)throw std::runtime_error("readback geometry/format");
 auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("readback HRESULT="+std::to_string(unsigned(hr)));};
 ID3D12Device*d=nullptr,*owner=nullptr;check(q->GetDevice(IID_PPV_ARGS(&d)));
 auto hr=source->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr)){d->Release();check(hr);}
 bool same=NativeSameDevice(owner,d);owner->Release();if(!same){d->Release();throw std::runtime_error("readback device mismatch");}
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
 D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;bd.DepthOrArraySize=bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 ID3D12Resource*readback=nullptr;hr=NativeCreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback));d->Release();check(hr);
 // Deliberately retain submission storage, readback and source on failure:
 // a timeout is not cancellation. Caller must not retry this unboundedly.
 source->AddRef();auto*submit=new NativeGameSubmission;submit->Create(q);
 submit->Submit([&](ID3D12GraphicsCommandList*c){
  D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,D3D12_RESOURCE_STATE_COPY_SOURCE};
  if(before!=D3D12_RESOURCE_STATE_COPY_SOURCE)c->ResourceBarrier(1,&b);
  D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;c->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
  std::swap(b.Transition.StateBefore,b.Transition.StateAfter);if(before!=D3D12_RESOURCE_STATE_COPY_SOURCE)c->ResourceBarrier(1,&b);
 });
 submit->Flush();
 /* bytes per pixel from the texture format: 8 for RGBA16, 4 for the 8-bit UNORM textures (Magpie); the rows are packed in the result */
 const size_t bpp=NativeIsRgba8Unorm(desc.Format)?4:8;
 std::vector<unsigned char>result(1920ull*1080*bpp);void*p=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(readback->Map(0,&range,&p));
 for(UINT y=0;y<1080;y++)std::memcpy(result.data()+size_t(y)*1920*bpp,static_cast<unsigned char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,1920*bpp);
 D3D12_RANGE none{};readback->Unmap(0,&none);delete submit;readback->Release();source->Release();return result;
}
