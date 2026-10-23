#include "/home/lmxxf/work/dlss5-gap-phase2-20261023/knife3/source/Development/HIP/packed_weights.h"
#include <iostream>
#include <algorithm>
#include <cassert>
int main(){
 const size_t N=512,K=512,bytes=N*K,tailStart=262144*4;
 for(unsigned pass=0;pass<3;pass++){
  std::vector<float> v(262656);auto*p=reinterpret_cast<uint8_t*>(v.data());
  std::fill(p,p+v.size()*4,0xa5);
  for(size_t i=0;i<bytes;i++)p[i]=uint8_t(i>>(pass*8));
  const auto original=v;
  hip_reference::FragmentPackedMatrixNPair(v,0,N,K);
  std::vector<unsigned> visits(bytes);
  for(unsigned row=0;row<512;row+=64)for(unsigned kt=0;kt<32;kt++)for(unsigned np=0;np<2;np++)for(unsigned gr=0;gr<2;gr++)for(unsigned rc=0;rc<16;rc++){
   unsigned n0=row+np*32+rc;
   const size_t at=((n0/32)*32+kt)*512+(gr*16+rc)*16;
   assert(at%16==0 && at+16<=bytes);
   for(unsigned j=0;j<2;j++)for(unsigned e=0;e<8;e++){
    const size_t offset=at+j*8+e;
    const size_t expected=(row+(np*2+j)*16+rc)*512+kt*16+gr*8+e;
    assert(p[offset]==uint8_t(expected>>(pass*8)));visits[offset]++;
   }
  }
  assert(std::all_of(visits.begin(),visits.end(),[](unsigned x){return x==1;}));
  assert(!std::memcmp(p+bytes,reinterpret_cast<const uint8_t*>(original.data())+bytes,v.size()*4-bytes));
  assert(tailStart+512*4==v.size()*4);
 }
 // Runtime grid and every token/channel address of the three real padded extents.
 for(unsigned n:{1504u,2048u,2160u}){
  std::vector<unsigned> seen(size_t(n)*512);
  for(unsigned bid=0;bid<n/2;bid++){
   unsigned first=bid/8*16,row=bid%8*64;
   for(unsigned gr=0;gr<2;gr++)for(unsigned rc=0;rc<16;rc++)for(unsigned j=0;j<4;j++)for(unsigned e=0;e<8;e++){
    size_t i=(first+gr*8+e)*512+row+j*16+rc;assert(i<seen.size());seen[i]++;
   }
  }
  assert(std::all_of(seen.begin(),seen.end(),[](unsigned x){return x==1;}));
 }
 std::cout << "PASS actual N-pair pack helper: 262144 unique matrix bytes x 3 coordinate-byte passes; all nonmatrix bytes unchanged; padded 1504/2048/2160 grids cover each output exactly once\n";
}
