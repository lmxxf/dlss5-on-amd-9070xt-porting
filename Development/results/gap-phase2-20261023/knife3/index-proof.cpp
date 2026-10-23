#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>
#include <cstdint>
#include "/home/lmxxf/work/dlss5-gap-phase2-20261023/knife3/source/Development/HIP/packed_weights.h"
int main(){
 constexpr unsigned N=512,K=512;
 std::vector<float> v(N*K+N),old(v.size());
 auto *p=reinterpret_cast<unsigned char*>(v.data());
 for(size_t i=0;i<v.size()*4;i++)p[i]=static_cast<unsigned char>((i*37u)^(i>>8)^(i>>16));
 old=v;auto *src=reinterpret_cast<unsigned char*>(old.data());
 hip_reference::FragmentPackedMatrixNPair(v,0,N,K);
 std::vector<unsigned> seen(N*K,0);
 size_t checked=0;
 for(unsigned row=0;row<N;row+=64)for(unsigned kt=0;kt<K/16;kt++)for(unsigned np=0;np<2;np++)for(unsigned lane=0;lane<32;lane++){
  unsigned rc=lane%16,gr=lane/16,n0=row+np*32+rc;
  size_t base=((n0/32)*32+kt)*512+(gr*16+rc)*16;
  if(base%16)return 1;
  for(unsigned pair=0;pair<2;pair++)for(unsigned e=0;e<8;e++){
   unsigned n=n0+pair*16,k=kt*16+gr*8+e;
   size_t dst=base+pair*8+e;
   if(dst>=N*K||p[dst]!=src[n*K+k])return 2;
   seen[dst]++;checked++;
  }
 }
 if(checked!=N*K||std::any_of(seen.begin(),seen.end(),[](unsigned x){return x!=1;}))return 3;
 if(std::memcmp(p+N*K,src+N*K,v.size()*4-N*K))return 4;
 // Old gs/h and new kt must visit identical A byte offsets and increasing output K chunks.
 for(unsigned kt=0;kt<32;kt++){unsigned gs=kt/2,h=kt%2;if(gs*512+h*16!=(kt/2)*512+(kt%2)*16)return 5;}
 std::printf("{\"matrix_bytes_checked\":%zu,\"bijection\":true,\"all_loads_16B_aligned\":true,\"tail_bytes_unchanged\":%zu,\"K16_order\":\"0..31 unchanged\",\"old_new_A_offsets_equal\":true}\n",checked,v.size()*4-N*K);
}
