#pragma once
#include <cstdint>
#include <stdexcept>

// External window pixels and network pixels are separate coordinate systems.
// Integer viewport dimensions keep padding boundaries on pixel edges.
struct NativeInputGeometry {
 unsigned width{},height{},x{},y{},fit_width{},fit_height{},network_width{1920},network_height{1080};
 static constexpr unsigned max_width=1920,max_height=1080;
 static constexpr uint64_t max_pixels=uint64_t(max_width)*max_height;
 static constexpr unsigned max_budget_width=2560;
 /* Without large, a wider input is admitted while it stays within the 1920x1080 pixel budget: 2024x848 (3440x1440 ultrawide
    at Quality 1) is 1.72M pixels, yet its width alone used to fail a per-axis cap. Such an input is downsampled onto the
    network surface just as FIT_LARGE would, so the width is capped at 2560 (at most 25% horizontal downsample, which
    covers 21:9 and 32:9 ultrawide within the budget) and the height stays within 1080. Everything the old 1920x1080 box
    admitted is still admitted. */
 /* large=true (DLSS5_FIT_LARGE=1): inputs beyond 1920x1080 are accepted and fitted like small ones, i.e. downsampled onto the
    network surface by the codec's bilinear fit and restored to the source extent before the host upscaler. */
 static bool Supported(uint64_t w,unsigned h,bool large=false){return w>0&&h>0&&w<=16384&&h<=16384&&(large||(w<=max_budget_width&&h<=max_height&&w*h<=max_pixels));}
 static NativeInputGeometry Make(unsigned w,unsigned h,unsigned nw=1920,unsigned nh=1080,bool large=false){
  if(!Supported(w,h,large))throw std::runtime_error("input exceeds this integration viewport limit");
  if(!((nw==1920&&nh==1080)||(nw==1280&&nh==720)||(nw==1600&&nh==900)))throw std::runtime_error("unsupported network viewport");
  NativeInputGeometry g{w,h,0,0,nw,nh,nw,nh};
  if(uint64_t(w)*nh>=uint64_t(h)*nw)g.fit_height=unsigned((uint64_t(h)*nw+w/2)/w);
  else g.fit_width=unsigned((uint64_t(w)*nh+h/2)/h);
  if(!g.fit_width)g.fit_width=1;if(!g.fit_height)g.fit_height=1;
  g.x=(nw-g.fit_width)/2;g.y=(nh-g.fit_height)/2;return g;
 }
 bool Adapted()const{return width!=1920||height!=1080||network_width!=1920||network_height!=1080;}
 unsigned RowPitch(unsigned bytes_per_pixel)const{return (width*bytes_per_pixel+255u)&~255u;}
};
