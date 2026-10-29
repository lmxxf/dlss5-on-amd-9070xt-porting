from pathlib import Path
import shutil,difflib,json
p=Path(__file__).parent;d=p/'work/2-host';assert not d.exists();(d/'Development/HIP').mkdir(parents=True);(d/'src').mkdir()
for folder in ['src','Development/HIP']:
 for f in Path(folder).iterdir():
  if f.is_file()and f.suffix in ['.h','.hpp','.cpp','.inc']:
   shutil.copyfile(f,d/folder/f.name)
f=d/'Development/HIP/hip_reference_network.h';old=f.read_text();key=' bool C32Norm900Active()const{return c32_norm900_loaded&&fast_numeric&&W==1600&&H==960&&multi_pass==1&&!opt.graph&&!opt.experimental_temporal;}'
assert key in old
new=''' static unsigned C32NormExpandMask(){static const unsigned m=[]{const char*v=std::getenv("NR_C32_NORM_EXPAND_MASK");return v?unsigned(std::strtoul(v,nullptr,0)):0u;}();return m;}
 bool C32NormExtraScope(bool active)const{
  const bool sm=active?small_active:requested_mode==0;unsigned bit=0;
  if(sm){if(W==1920&&H==1152)bit=1;else if(W==1600&&H==1024)bit=2;else if(W==1344&&H==768)bit=4;else if(W==896&&H==512)bit=8;}
  else if(W==1920&&H==1088&&multi_pass==1)bit=16;
  return (C32NormExpandMask()&bit)!=0;
 }
 bool C32Norm900Active()const{return c32_norm900_loaded&&fast_numeric&&((W==1600&&H==960)||C32NormExtraScope(true))&&multi_pass==1&&!opt.graph&&!opt.experimental_temporal;}'''
s=old.replace(key,new);key='const bool geometry=fast_numeric&&W==1600&&H==960&&!opt.graph&&!opt.experimental_temporal;';assert key in s;s=s.replace(key,'const bool geometry=fast_numeric&&((W==1600&&H==960)||C32NormExtraScope(false))&&!opt.graph&&!opt.experimental_temporal;');f.write_text(s);(p/'2-host.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/Development/HIP/hip_reference_network.h',tofile='b/Development/HIP/hip_reference_network.h')))
shutil.copyfile('Development/HIP/benchmark_live_capture.cpp',d/'frame.cpp')
