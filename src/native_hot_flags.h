#pragma once
#include "native_lab_paths.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <mutex>
/* Hot reload of the flags file (0.38, 2026-09-30; after mochizuki 0.0.2.4's ini reload).
   DLSS5_HOT_RELOAD=1 (default; environment, else the flags file at start): at most once a second the add-on compares the
   last-write time of the three config layers (default-config.txt, custom-config.txt, native-game-flags.txt; native_config_layers.h); when it changed, it re-reads ONLY these keys and applies them from the next frame:
     DLSS5_STRENGTH   transfer,colour (0..3) or auto  -- decoder blend after the network, not the network itself
     DLSS5_NOTICE     on-screen status line (0/1/2)
     DLSS5_SHOW_FPS   FPS text in that line (0/1)
     DLSS5_MULTI_PASS pass count 1/2/3 (2026-10-03; HIP network, applied before the next network frame; invalid/absent = 1)
     DLSS5_MULTI_PASS_PREDICT 0/1; default 1; only active with 3 passes (LOSSY)
     DLSS5_MULTI_PASS_SKIN_PROTECT 0/1; default 0; skin-color heuristic blend at N>1
   Everything else (network height, skip blocks, HIP kernels/modules, FP8/wave/tiling switches, DIRECT_IO, PRE_UPSCALE, FIT_*, ASYNC,
   FRAME_STATS, FORMAT_FALLBACK) stays as read at start: those build buffers, modules or pipelines once, or change the network's
   numbers; editing them needs a game restart as before. The first poll only records the time stamp, so an unedited file changes
   nothing (bit-exact). A reload is logged to logs\native-game-oneshot.txt. Cost: one GetTickCount64 per call, one stat per second.
   0 = never poll (previous behaviour). */
struct NativeHotFlagValues{bool strength=false;float transfer=1.f,color=1.f;int notice=-1,fps=-1,multi_pass=-1,multi_pass_predict=-1,multi_pass_skin_protect=-1;unsigned generation=0;};
class NativeHotFlags{
 std::mutex mutex;NativeHotFlagValues values;std::atomic<bool> hotkey_down{false},force_reload{false}; /* force: set by the hotkey after writing (a write in the same file-time tick would not change the stamp) */ULONGLONG last_poll=0;FILETIME stamp{};bool stamped=false;
 /* Stamp of the three layers (default / custom / native): write time of each, 0 for a missing file, folded into one value so that
    editing, creating or deleting any layer counts as a change. */
 static bool read_stamp(FILETIME&t){ULONGLONG h=0;bool any=false;
  for(const wchar_t*name:NativeConfigLayerNames){WIN32_FILE_ATTRIBUTE_DATA a{};ULONGLONG w=0;
   if(GetFileAttributesExW(NativeLabPath(name).c_str(),GetFileExInfoStandard,&a)){any=true;w=(ULONGLONG(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime;}
   h=h*1000003ull^w;}
  if(!any)return false;t.dwLowDateTime=DWORD(h);t.dwHighDateTime=DWORD(h>>32);return true;}
 void reload(){
  NativeHotFlagValues v;v.generation=values.generation+1;char strength[48]="";
  /* mid-save by an editor (no layer readable at all): keep the previous values, retry on the next stamp change */
  if(!NativeConfigDirHasAny(NativeLabRoot()))return;
  for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();{unsigned n;
   if(sscanf(line,"DLSS5_NOTICE=%u",&n)==1)v.notice=int(n);if(sscanf(line,"DLSS5_SHOW_FPS=%u",&n)==1)v.fps=int(n);sscanf(line,"DLSS5_STRENGTH=%47s",strength);
   if(!strncmp(line,"DLSS5_MULTI_PASS_PREDICT=",25)){const char*m=line+25;v.multi_pass_predict=(!*m||!strcmp(m,"1"))?1:!strcmp(m,"0")?0:(std::fprintf(stderr,"DLSS5_MULTI_PASS_PREDICT=%s invalid (0/1), using 0\n",m),0);}
   if(!strncmp(line,"DLSS5_MULTI_PASS_SKIN_PROTECT=",30)){const char*m=line+30;v.multi_pass_skin_protect=(!*m||!strcmp(m,"0"))?0:!strcmp(m,"1")?1:(std::fprintf(stderr,"DLSS5_MULTI_PASS_SKIN_PROTECT=%s invalid (0/1), using 0\n",m),0);}
   if(!strncmp(line,"DLSS5_MULTI_PASS=",17)){const char*m=line+17;v.multi_pass=(!*m||!strcmp(m,"1"))?1:!strcmp(m,"2")?2:!strcmp(m,"3")?3:(std::fprintf(stderr,"DLSS5_MULTI_PASS=%s invalid (1/2/3), using 1\n",m),1);}}}
  if(v.multi_pass<0)v.multi_pass=1; /* key gone from every layer: built-in default */
  if(v.multi_pass_predict<0)v.multi_pass_predict=1; /* key removed: built-in default */
  if(v.multi_pass_skin_protect<0)v.multi_pass_skin_protect=0; /* key removed: built-in default */
  float a=1.f,b=1.f;if(sscanf(strength,"%f,%f",&a,&b)==2&&a>=0.f&&a<=3.f&&b>=0.f&&b<=3.f){v.strength=true;v.transfer=a;v.color=b;}
  values=v;
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=hot_reload detail=generation %u strength=%s notice=%d show_fps=%d multi_pass=%d multi_pass_predict=%d multi_pass_skin_protect=%d (other keys need a restart)\n",GetCurrentProcessId(),GetTickCount64(),v.generation,v.strength?strength:"startup",v.notice,v.fps,v.multi_pass,v.multi_pass_predict,v.multi_pass_skin_protect);fclose(f);}
 }
public:
 static bool Enabled(){
  static const bool on=[]{if(const wchar_t*e=_wgetenv(L"DLSS5_HOT_RELOAD"))return wcscmp(e,L"0")!=0;
   unsigned x=1;for(const std::string&cfg_line:NativeConfigFileLines()){const char*line=cfg_line.c_str();sscanf(line,"DLSS5_HOT_RELOAD=%u",&x);}return x!=0;}();
  return on;
 }
 /* Current values (defaults = "use what was read at start"). Polls the file stamp at most once per second. */
 NativeHotFlagValues Get(){
  if(!Enabled())return {};
  std::lock_guard<std::mutex>lock(mutex);const ULONGLONG now=GetTickCount64();
  if(!last_poll||now-last_poll>=1000){last_poll=now;FILETIME t{};
   if(read_stamp(t)){if(force_reload.exchange(false)){stamp=t;stamped=true;reload();}else if(!stamped){stamp=t;stamped=true;}else if(CompareFileTime(&t,&stamp)!=0){stamp=t;reload();}}}
  return values;
 }
 /* DLSS5_MULTI_PASS_HOTKEY (2026-10-03): default F9; "F1".."F24" or a virtual-key number (e.g. 0x78); 0 = off. Edge-triggered,
    polled by the HIP network once per frame. A press cycles 1->2->3->1 starting from the pass count in effect and writes
    DLSS5_MULTI_PASS=N into custom-config.txt (the line replaced, else appended; the file created if missing, BOM and line ends kept).
    If native-game-flags.txt also has the key (it would win over custom), that line is rewritten as well, with a stderr line.
    A DLSS5_MULTI_PASS system environment variable wins over every file: stderr line, files still written. The hot reload above
    then applies the value (so DLSS5_HOT_RELOAD=0 disables the effect). Returns the new count (0 = no press). Not pressed = no file access. */
 static unsigned HotkeyCode(){
  static const unsigned vk=[]{std::string v="F9";for(const std::string&l:NativeConfigFileLines())if(!l.compare(0,24,"DLSS5_MULTI_PASS_HOTKEY="))v=l.substr(24);
   if(const char*e=std::getenv("DLSS5_MULTI_PASS_HOTKEY"))v=e;
   if(v.empty()||v=="0")return 0u;
   if((v[0]=='F'||v[0]=='f')&&v.size()>1){char*end=nullptr;unsigned long n=strtoul(v.c_str()+1,&end,10);if(!*end&&n>=1&&n<=24)return unsigned(VK_F1+n-1);}
   char*end=nullptr;unsigned long n=strtoul(v.c_str(),&end,0);if(!*end&&n>0&&n<256)return unsigned(n);
   std::fprintf(stderr,"DLSS5_MULTI_PASS_HOTKEY=%s invalid (F1..F24, key code, 0), using F9\n",v.c_str());return unsigned(VK_F9);}();
  return vk;}
 static bool RewriteKey(const wchar_t*name,const char*key,const std::string&value,bool create){
  const std::wstring path=NativeLabPath(name);std::string text;const bool exists=NativeConfigReadFile(path,text);if(!exists&&!create)return false;
  const std::string nl=text.find("\r\n")!=std::string::npos?"\r\n":"\n";std::string out;bool done=false;size_t at=0;const std::string want=std::string(key)+"="+value;
  while(at<text.size()){size_t e=text.find('\n',at);const size_t next=e==std::string::npos?text.size():e+1;std::string line=text.substr(at,next-at),k,v;
   if(NativeConfigParseLine(line,k,v)&&k==key){const size_t body=line.find_last_not_of("\r\n")+1;out+=(at==0&&line.compare(0,3,"\xEF\xBB\xBF")==0?"\xEF\xBB\xBF":"")+want+line.substr(body);done=true;}
   else out+=line;at=next;}
  if(!done){if(!out.empty()&&out.back()!='\n')out+=nl;out+=want+nl;}
  const std::wstring tmp=path+L".tmp";FILE*f=_wfopen(tmp.c_str(),L"wb");if(!f)return false;const bool ok=std::fwrite(out.data(),1,out.size(),f)==out.size();std::fclose(f);
  if(!ok||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)){DeleteFileW(tmp.c_str());return false;}return true;}
 unsigned PollMultiPassHotkey(unsigned current,bool allow_cycle=true){
  const unsigned vk=HotkeyCode();if(!vk)return 0;const bool down=(GetAsyncKeyState(int(vk))&0x8000)!=0;
  if(!down||hotkey_down.exchange(down)){if(!down)hotkey_down=false;return 0;}
  return CycleMultiPass(current,allow_cycle);}
 /* The press itself (separate for tests): writes the files, logs, returns the new count. */
 static unsigned CycleMultiPass(unsigned current,bool allow_cycle=true){
  // Consume the key edge before rejecting: never persist an unsupported pass count.
  if(!allow_cycle){if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=multi_pass_hotkey_blocked reason=active_temporal_mode_requires_MP1\n",GetCurrentProcessId(),GetTickCount64());fclose(f);}return 0;}
  const unsigned next=current%3+1;const std::string n=std::to_string(next);
  const bool custom=RewriteKey(L"custom-config.txt","DLSS5_MULTI_PASS",n,true);
  std::string native;bool native_has=false;
  if(NativeConfigReadFile(NativeLabPath(L"native-game-flags.txt"),native))native_has=NativeConfigFind(NativeConfigParseText(native),"DLSS5_MULTI_PASS")!=nullptr;
  bool native_ok=false;if(native_has){std::fprintf(stderr,"DLSS5_MULTI_PASS_HOTKEY: native-game-flags.txt also sets DLSS5_MULTI_PASS (it overrides custom-config.txt); rewriting that line too\n");native_ok=RewriteKey(L"native-game-flags.txt","DLSS5_MULTI_PASS",n,false);}
  const bool env=NativeConfigFind(NativeConfigSystemEnvironment(),"DLSS5_MULTI_PASS")!=nullptr;
  if(env)std::fprintf(stderr,"DLSS5_MULTI_PASS_HOTKEY: a DLSS5_MULTI_PASS environment variable overrides the config files; the change will not apply\n");
  Instance().force_reload=true;
  if(FILE*f=_wfopen(NativeLabPath(L"logs\\native-game-oneshot.txt").c_str(),L"ab")){fprintf(f,"pid=%lu tick=%llu event=multi_pass_hotkey detail=%u->%u custom=%s native=%s env_override=%u\n",GetCurrentProcessId(),GetTickCount64(),current,next,custom?"written":"FAILED",native_has?(native_ok?"rewritten":"FAILED"):"no-key",unsigned(env));fclose(f);}
  return next;}
 static NativeHotFlags&Instance(){static NativeHotFlags*h=new NativeHotFlags;return *h;}
};
