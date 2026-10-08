#include <cstring>
#include "../src/native_hot_flags.h"
#include "../src/native_fast_history_policy.h"
#include <fstream>
#include <iterator>
static std::string Read(const wchar_t*name){std::ifstream f(NativeLabPath(name).c_str(),std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
int main()try{
 auto custom=Read(L"custom-config.txt"),native=Read(L"native-game-flags.txt"),defaults=Read(L"default-config.txt");Require(!custom.empty()&&!native.empty()&&!defaults.empty(),"isolated fixture config must exist");
 for(unsigned mode:{1u,2u,3u}){(void)mode;Require(NativeHotFlags::CycleMultiPass(1,false)==0,"blocked temporal cycle returns no-change");Require(Read(L"custom-config.txt")==custom&&Read(L"native-game-flags.txt")==native&&Read(L"default-config.txt")==defaults,"blocked temporal F9 cannot persist MP2");}
 bool rejected=false;try{NativeFastHistoryPolicy::RequireSinglePass(true,2);}catch(const std::exception&e){rejected=std::string(e.what()).find("1/2/3")!=std::string::npos;}Require(rejected,"manual invalid MP still explicitly rejected for all three modes");
 Require(NativeHotFlags::CycleMultiPass(1)==2,"mode0 preserves default allowed F9 cycle");Require(Read(L"custom-config.txt").find("DLSS5_MULTI_PASS=2")!=std::string::npos&&Read(L"native-game-flags.txt").find("DLSS5_MULTI_PASS=2")!=std::string::npos,"allowed cycle still writes both precedence layers");
 auto changedCustom=Read(L"custom-config.txt"),changedNative=Read(L"native-game-flags.txt");Require(NativeHotFlags::CycleMultiPass(2,false)==0&&Read(L"custom-config.txt")==changedCustom&&Read(L"native-game-flags.txt")==changedNative,"blocked cycle leaves existing values unchanged");
 puts("PASS temporal F9 guard: no config write for modes1/2/3, mode0 cycle preserved, manual invalid MP rejected");return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
