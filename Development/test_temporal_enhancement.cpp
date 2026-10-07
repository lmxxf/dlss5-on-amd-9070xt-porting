#include <cstring>
#include "fast_history_fixture.h"
#include "../src/native_low_frequency_temporal.h"
#include "../src/native_temporal_mode.h"
#include <array>
// Independent double precision formula for the newly added correction only.
static std::array<double,3> Extra(std::array<double,3>low,std::array<double,3>high,double strength){std::array<double,3> r{};double peak=0;for(int c=0;c<3;c++){r[c]=strength*(.5*low[c]+high[c]);peak=std::max(peak,std::abs(r[c]));}if(peak>.04){double z=peak-.04,limit=.04+z/(1+z/.12);for(auto&v:r)v*=limit/peak;}return r;}
int main(int argc,char**)try{
 Gpu g(argc>1);constexpr UINT w=8,h=8,n=w*h;auto raw=g.Buffer(n*16),out=g.Buffer(n*12);std::vector<float>x(n*4,.5f),y(n*3,.5f);g.Upload(raw.Get(),x);
 auto run=[&](NativeLowFrequencyTemporal&f,UINT id,bool reset){g.Upload(out.Get(),y);f.Prepare(g.queue.Get(),id,true,1,reset,nullptr,0,0,0,0,false,.08*std::log(2.));g.Run([&](auto*c){f.Record(c,raw.Get(),out.Get());});f.Submitted(g.queue.Get());auto result=g.Read(out.Get());for(float v:result)Require(std::isfinite(v)&&v>=0&&v<=1,"finite codec-range output");return result;};
 NativeLowFrequencyTemporal neutral;neutral.Create(g.device.Get(),w,h,h,true,0);
 std::fill(y.begin(),y.end(),.53125f);Require(run(neutral,0,true)==y,"zero strength cold bitidentity");std::fill(y.begin(),y.end(),.5625f);Require(run(neutral,1,false)==y&&neutral.UsingPast()==1,"zero strength warm bitidentity despite history");
 NativeLowFrequencyTemporal active;active.Create(g.device.Get(),w,h,h,true,1);
 std::fill(y.begin(),y.end(),.53125f);auto low=run(active,0,true);for(float v:low)Require(v==.546875f,"cold constant residual gain1.5 not just antiflicker");
 active.Reset();for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)y[i*3+c]=.5f+(((i%w+i/w)&1)? .015625f:-.015625f);auto high=run(active,1,true);for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)Require(high[i*3+c]==.5f+(((i%w+i/w)&1)? .03125f:-.03125f),"cold zero-mean high residual gain2");
 active.Reset();std::array<double,3> r{.25,.125,.0625};for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)y[i*3+c]=float(.5+r[c]);auto compressed=run(active,2,true);auto extra=Extra(r,{0,0,0},1);for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)Require(std::abs(compressed[i*3+c]-(y[i*3+c]+extra[c]))<3e-6,"strong RGB increment softcompressed by common positive scale");Require(extra[0]<.125&&std::abs(extra[0]/extra[1]-2)<1e-12,"compression direction and magnitude");
 active.Reset();std::array<double,3>edge{.96875,.9375,.875},edgeLow{};for(int c=0;c<3;c++)edgeLow[c]=edge[c]-.5;for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)y[i*3+c]=float(edge[c]);auto boundary=run(active,3,true);auto edgeExtra=Extra(edgeLow,{0,0,0},1);double room=1;for(int c=0;c<3;c++)room=std::min(room,(1-edge[c])/edgeExtra[c]);for(UINT i=0;i<n;i++)for(int c=0;c<3;c++)Require(std::abs(boundary[i*3+c]-(edge[c]+room*edgeExtra[c]))<4e-6,"near-gamut RGB correction keeps common direction scaling");
 std::fill(y.begin(),y.end(),.5f);Require(run(active,4,false)==y,"zero residual cannot resurrect old low history");std::fill(y.begin(),y.end(),0);Require(run(active,5,false)==y,"failed black NN not enhanced or resurrected");
 std::fill(y.begin(),y.end(),.53125f);Require(run(active,6,true)==low,"reset discards history but immediate spatial enhancement remains");
 g.NoErrors();puts("MODE3 PASS: strength0 cold/warm identity; low1.5/high2 cold gains; independent compression; zero/reset/failed-output guards");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
