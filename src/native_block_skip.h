#pragma once
// Block-skip probe (DLSS5_SKIP_BLOCKS="5,6,17"): a listed residual block is replaced by a copy of its input into its output
// buffer (identity), so the bench can map how much each block costs and how much PSNR it is worth. Only blocks whose input
// and output share a layout may be listed (checked by byte size). Diagnostic only; never enable in the game build.
#include <set>
#include <string>
#include <map>
#include <d3d12.h>
inline const std::set<unsigned>&NativeSkipBlocks(){static std::set<unsigned>s=[]{std::set<unsigned>r;if(const wchar_t*v=_wgetenv(L"DLSS5_SKIP_BLOCKS")){std::wstring t(v);size_t p=0;while(p<t.size()){size_t q=t.find(L',',p);if(q==std::wstring::npos)q=t.size();if(q>p)r.insert(unsigned(wcstoul(t.substr(p,q-p).c_str(),nullptr,10)));p=q+1;}}return r;}();return s;}
inline bool NativeSkipBlock(unsigned block){return NativeSkipBlocks().count(block)!=0;}
// Copies src (in NON_PIXEL_SHADER_RESOURCE state) into dst and leaves both readable. dst starts in UAV state the first time.
inline void NativeSkipCopy(ID3D12GraphicsCommandList*c,ID3D12Resource*src,ID3D12Resource*dst,unsigned block){
 static std::map<ID3D12Resource*,bool>touched;
 const UINT64 bytes=src->GetDesc().Width;if(dst->GetDesc().Width!=bytes)throw std::runtime_error("skip block "+std::to_string(block)+": input/output layouts differ");
 D3D12_RESOURCE_BARRIER b[2]{};for(auto&v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b[0].Transition={src,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
 b[1].Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,touched[dst]?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};
 c->ResourceBarrier(2,b);c->CopyBufferRegion(dst,0,src,0,bytes);
 b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 c->ResourceBarrier(2,b);touched[dst]=true;
}
