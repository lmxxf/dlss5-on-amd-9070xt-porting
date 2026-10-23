#include <cassert>
#include <iostream>
struct S{unsigned W,H;bool small_active;
 // Central shape admission for the paired contract/projection tail paths.
 bool CompactVitTailGeometryEligible()const{
  if(W==1920&&H==1152)return true; // Existing Original and Small 1080 path.
  if(!small_active)return (W==1920&&H==1088)||(W==1600&&H==960);
  return (W==1600&&H==1024)||(W==1344&&H==768)||(W==1920&&H==1088);
 }
};
int main(){unsigned checks=0;
for(bool small:{false,true})for(unsigned W:{512u,1280u,1344u,1600u,1920u,2560u})for(unsigned H:{512u,768u,960u,1024u,1088u,1152u,1472u}){
 S s{W,H,small};bool old=(W==1920&&H==1152)||(!small&&((W==1920&&H==1088)||(W==1600&&H==960)));
 bool added=small&&((W==1600&&H==1024)||(W==1344&&H==768)||(W==1920&&H==1088));
 assert(s.CompactVitTailGeometryEligible()==(old||added));++checks;
}
std::cout<<"PASS actual geometry helper "<<checks<<" combinations; only three Small scopes added\n";
}
