#include "../src/native_fast_history_policy.h"
#include <iostream>
#include <string>
int main(){try {
 for(unsigned n:{1u,2u,3u})NativeFastHistoryPolicy::RequireSinglePass(false,n);
 NativeFastHistoryPolicy::RequireSinglePass(true,1);
 for(unsigned n:{2u,3u}) { bool rejected=false;try{NativeFastHistoryPolicy::RequireSinglePass(true,n);}catch(const std::runtime_error&e){rejected=std::string(e.what()).find("restart")!=std::string::npos;}if(!rejected)return 1;}
 // A combined hot snapshot must be admitted before any setters run.
 unsigned live=1;bool prediction=false,skin=false;try{NativeFastHistoryPolicy::RequireSinglePass(true,3);prediction=skin=true;live=3;}catch(const std::runtime_error&){}
 if(live!=1||prediction||skin)return 1;
 std::cout<<"PASS default-off MP1/2/3 and Fast History startup/hot snapshot MP1 scope\n";return 0;
}catch(...){return 1;}}
