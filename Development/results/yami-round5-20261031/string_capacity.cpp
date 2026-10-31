#include <string>
#include <cstdio>
int main(){std::string f="vit_attention_fused_640";f+="_bytein";f+="_bout";f+="_qkfrag";auto old=f.c_str();auto cap=f.capacity(),len=f.size();f+="_rtzpair";printf("SELECTOR old_len=%zu old_capacity=%zu appended_len=%zu appended_capacity=%zu reallocated=%u\n",len,cap,f.size(),f.capacity(),unsigned(old!=f.c_str()));return 0;}
