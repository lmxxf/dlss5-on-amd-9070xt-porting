from pathlib import Path
import shutil,subprocess,sys,difflib
here=Path(__file__).resolve().parent;root=here.parents[3];out=Path('/tmp/vit-residual-src')
# Use the validated exact-stream operator as the foundation, not an approximate-attention candidate.
subprocess.run([sys.executable,str(here.parent/'vit-stream-exact/prepare.py')],check=True)
for p in ['src','Development/HIP']:shutil.copytree(root/p,out/p,dirs_exist_ok=True)
(out/'kernel').mkdir(exist_ok=True);(out/'kernel/deep_fast.hip').write_text(Path('/tmp/vit-stream-exact-src/deep_fast.hip').read_text()+'\n'+(here/'reuse.hip').read_text())
p=out/'Development/HIP/hip_reference_network.h';old=p.read_text();s=old
method=r''' Tensor residual_anchor_in,residual_anchor_out;U residual_frame{},residual_n{},residual_anchor_frame{};
 void ResidualDump(const char*dir,const char*part,Tensor t){
  Synchronize();std::vector<char>bytes(t->bytes);api.Check(api.hipMemcpy(bytes.data(),P(t),bytes.size(),2),"residual capture");
  auto name=std::string(dir)+"/"+std::to_string(residual_frame)+"-"+part+".f32";FILE*f=fopen(name.c_str(),"wb");if(!f)throw std::runtime_error("residual dump open");bool ok=fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();fclose(f);if(!ok)throw std::runtime_error("residual dump write");
 }
 Tensor ResidualVitGroup(Tensor input,U n){
  ++residual_frame;const char*period_str=std::getenv("DLSS5_VIT_REUSE_PERIOD");U period=period_str?U(std::stoul(period_str)):0;
  if(period>1000)throw std::runtime_error("reuse period exceeds diagnostic limit");
  const char*dump=std::getenv("DLSS5_VIT_RESIDUAL_DUMP");const char*log=std::getenv("DLSS5_VIT_RESIDUAL_LOG");
  if((period||dump)&&opt.graph)throw std::runtime_error("residual experiment requires graph off");
  bool reuse=period>1&&residual_anchor_in&&residual_n==n&&((residual_frame-1)%period)!=0;
  if(dump&&*dump)ResidualDump(dump,"input",input);
  Tensor result;
  if(reuse){result=New(size_t(n)*1024);Run("deep","vit_residual_apply",size_t(n)*1024,P(input),P(residual_anchor_in),P(residual_anchor_out),P(result),U(n*1024));}
  else{result=input;for(U block=31;block<=38;block++)result=Vit(result,n,block);
   if(period>1){residual_anchor_in=input;residual_anchor_out=result;residual_n=n;residual_anchor_frame=residual_frame;}}
  if(dump&&*dump)ResidualDump(dump,"output",result);
  if(log&&*log){FILE*f=fopen(log,"ab");if(!f)throw std::runtime_error("residual log open");fprintf(f,"%u,%u,%u,%u,%u\n",residual_frame,n,period,unsigned(reuse),residual_anchor_frame);fclose(f);}
  return result;
 }
'''
needle='for(U b=31;b<=38;b++)source=Vit(source,n,b);';assert s.count(needle)==1
s=s.replace(needle,'source=ResidualVitGroup(source,n);')
s=s.replace(' Tensor Vit(Tensor input,U n,U block){',method+' Tensor Vit(Tensor input,U n,U block){',1)
# Explicitly release persistent anchors before destroying the stream.
s=s.replace('device_noise.reset();gather_maps[0].clear();','residual_anchor_in.reset();residual_anchor_out.reset();device_noise.reset();gather_maps[0].clear();')
p.write_text(s);(here/'host.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/Development/HIP/hip_reference_network.h',tofile='b/Development/HIP/hip_reference_network.h')))
p=out/'Development/HIP/benchmark_live_capture.cpp';s=p.read_text()
s=s.replace(' std::ifstream source(argv[3]',r''' const char*seq_str=std::getenv("DLSS5_RESIDUAL_SEQUENCE");const UINT sequence=seq_str?UINT(std::stoul(seq_str)):0;
 const bool capture_rgb=std::getenv("DLSS5_RESIDUAL_RGB")&&strcmp(std::getenv("DLSS5_RESIDUAL_RGB"),"0");
 if(sequence>4||(sequence&&N>32))throw std::runtime_error("controlled sequence limits");
 std::ifstream source(argv[3]''',1)
needle='for(UINT i=0;i<N;i++){// Restore the SAME original HDR texture; never process the previous output recursively.';assert needle in s
s=s.replace(needle,r'''for(UINT i=0;i<N;i++){
 if(sequence){void*mp=nullptr;ck(up->Map(0,&none,&mp));for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++){
  UINT sx=sequence==1?(x+W-i%W)%W:sequence==3&&i>=N/2?W-1-x:x;
  uint16_t rgba[4];memcpy(rgba,frozen.data()+(size_t(y)*W+sx)*4,8);
  if(sequence==2)for(unsigned c=0;c<3;c++)rgba[c]=Float16ForSequence(NativeHalfToFloat(rgba[c])*(1.f+.01f*i));
  if(sequence==4&&i>=N/2&&x>=3*W/5&&x<4*W/5&&y>=H/8&&y<3*H/5)for(unsigned c=0;c<3;c++)rgba[c]=0;
  memcpy(static_cast<char*>(mp)+fp.Offset+size_t(y)*fp.Footprint.RowPitch+size_t(x)*8,rgba,8);
 }up->Unmap(0,nullptr);}
 // Each step restores its own controlled input; sequence 0 is frozen.
''')
s=s.replace('auto s=stats(last,W,H);','if(capture_rgb)save_frame(prefix+L"-frame-"+std::to_wstring(i),last,W,H);auto s=stats(last,W,H);')
# Let the compiler perform IEEE binary16 conversion for the synthetic exposure input.
s=s.replace('int wmain(int argc,wchar_t**argv)', 'static uint16_t Float16ForSequence(float x){_Float16 h=static_cast<_Float16>(x);uint16_t u;memcpy(&u,&h,2);return u;}\nint wmain(int argc,wchar_t**argv)')
p.write_text(s);print(out)
