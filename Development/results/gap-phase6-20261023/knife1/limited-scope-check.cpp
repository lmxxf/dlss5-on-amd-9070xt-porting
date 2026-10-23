#include <cassert>
#include <initializer_list>
using U=unsigned;
struct P{bool small_active;U W,H; bool VitQKFragmentShapeEligible(U n)const{
  return small_active?((W==1600&&H==1024&&n==448)||(W==1920&&H==1152&&n==640)):(W==1600&&H==960&&n==400);
 }
};
int main(){unsigned cases=0,accepted=0;for(bool sm:{false,true})for(U w:{1344u,1600u,1920u})for(U h:{768u,960u,1024u,1088u,1152u})for(U n:{288u,400u,448u,640u}){P p{sm,w,h};bool expect=sm?((w==1600&&h==1024&&n==448)||(w==1920&&h==1152&&n==640)):(w==1600&&h==960&&n==400);assert(p.VitQKFragmentShapeEligible(n)==expect);++cases;accepted+=expect;}assert(cases==120&&accepted==3);}