import pathlib,shutil,subprocess,json,hashlib
R=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297');W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase2-20261023/knife3');O=pathlib.Path(__file__).parent
S=W/'source';shutil.copytree(R/'hip',S/'hip',dirs_exist_ok=True);shutil.copytree(R/'src',S/'src',dirs_exist_ok=True);shutil.copytree(R/'Development/HIP',S/'Development/HIP',dirs_exist_ok=True)
p=S/'hip/deep_fast.hip';s=p.read_text();old='template<bool W32,bool F16Res=false,bool ByteSkip=false,bool Transposed=false> DEV void split_projection_frag_w16_body';assert s.count(old)==1;s=s.replace(old,old.replace('Transposed=false>','Transposed=false,bool NPair=false>'))
start=s.index('template<bool W32,bool F16Res=false,bool ByteSkip=false,bool Transposed=false,bool NPair=false>')
a=s.index(' f8 acc[4]{};\n for(uint j=0;j<4;j++)for(uint e=0;e<8;e++)',start)
l0=s.index(' _Pragma("unroll 4") for(uint gs=0;gs<16;gs++){',a);l1=s.index('\n#if C512_T8_TAIL_VEC',l0)
oldloop=s[l0:l1]
newloop=''' if constexpr(NPair){
  // Pair adjacent N16 fragments in each 16B load; K16 order per output remains 0..31.
  _Pragma("unroll 4") for(uint kt=0;kt<32;kt++){
   i2 av;__builtin_memcpy(&av,arow+(kt/2)*512+(kt%2)*16,8);
   _Pragma("unroll") for(uint np=0;np<2;np++){
    uint n0=row+np*32+rc();w16_i4 bw;
    __builtin_memcpy(&bw,wb+((n0/32)*32+kt)*512+(gr()*16+rc())*16,16);
    i2 b0={bw[0],bw[1]},b1={bw[2],bw[3]};
    acc[np*2]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(av,b0,acc[np*2]);
    acc[np*2+1]=__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(av,b1,acc[np*2+1]);
   }
  }
 }else{
'''+oldloop+'\n }'
s=s[:l0]+newloop+s[l1:]
needle='WAVE void split_projection_frag_w16_nof32('
pos=s.index(needle,start)
exports='''// Experiment-only N-pair exports; independent host cache key prevents W16 layout aliasing.
WAVE void split_projection_frag_npair(const unsigned char*in8,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens){split_projection_frag_w16_body<true,false,false,false,true>(in8,w,skip,out,out8,nullptr,tokens);}
WAVE void split_projection_frag_npair_bskip(const unsigned char*in8,const float*w,const float*skip,float*out,unsigned char*out8,uint tokens){split_projection_frag_w16_body<true,false,true,false,true>(in8,w,skip,out,out8,nullptr,tokens);}
'''
s=s[:pos]+exports+s[pos:];p.write_text(s)
p=S/'Development/HIP/packed_weights.h';s=p.read_text();pos=s.index('inline void TilePackedMatrix(')
helper='''// N-pair tiles: [N/32][K/16][lane-half][N%16][N16 in N32][8 consecutive K].
// Input is already row-major FP8 at its original byte address; preserve all scale/bias tails.
inline void FragmentPackedMatrixNPair(std::vector<float>&v,size_t start,size_t rows,size_t columns){
 if(rows%32||columns%16||start>v.size()||rows*columns>(v.size()-start)*4)throw std::runtime_error("packed N-pair shape");
 auto*dst=reinterpret_cast<uint8_t*>(v.data()+start);std::vector<uint8_t>src(dst,dst+rows*columns);
 for(size_t n=0;n<rows;n++)for(size_t k=0;k<columns;k++)dst[((n/32)*(columns/16)+k/16)*512+((((k%16)/8)*16+n%16)*2+(n%32)/16)*8+k%8]=src[n*columns+k];
}
'''
s=s[:pos]+helper+s[pos:];p.write_text(s)
p=S/'Development/HIP/hip_reference_network.h';s=p.read_text();pos=s.index(' void* PackedMhWeightQkvFrag(')
helper=''' void* PackedSplitProjectionNPair(const std::string&name){const auto key=name+"@proj-npair-v1";auto it=weights.find(key);if(it==weights.end()){auto v=ReadWeights(opt.assets+"/"+name);if(v.size()!=WeightElements(name))throw std::runtime_error("split N-pair weight shape");Fp8(v,{{0,262144}});FragmentPackedMatrixNPair(v,0,512,512);it=weights.emplace(key,UploadWeight(v,key)).first;}return P(it->second);}
'''
s=s[:pos]+helper+s[pos:]
needle='kernel=="split_projection_frag_w16"||kernel=="split_projection_frag_w16_bskip"||';assert s.count(needle)==1;s=s.replace(needle,needle+'kernel=="split_projection_frag_npair"||kernel=="split_projection_frag_npair_bskip"||')
pos=s.index(' unsigned long long c512_w16_calls=')
s=s[:pos]+''' bool C512NPairRequested()const{static const bool on=[](){const char*v=std::getenv("NR_C512_NPAIR");return v&&std::strcmp(v,"1")==0;}();return on;}
 unsigned long long c512_npair_calls=0;
'''+s[pos:]
needle='  if(!fpf)Run("deep",w16_fb8?';pos=s.index(needle)
line_end=s.index('\n',pos);oldline=s[pos:line_end]
new='''  const bool npair=!small_active&&fast_numeric&&!fpf&&!w16_fb8&&!fb8&&w16_ffn&&C512NPairRequested()&&HasFn("deep_fast","split_projection_frag_npair")&&HasFn("deep_fast","split_projection_frag_npair_bskip");
  if(npair){++c512_npair_calls;Run("deep",byte_in?"split_projection_frag_npair_bskip":"split_projection_frag_npair",size_t(n)*512,P(contract8),PackedSplitProjectionNPair(Block(block,"ffwd-projection")),P(compact),P(ffn),P(ffn8),n);}
  else '''+oldline[2:]
s=s[:pos]+new+s[line_end:]
needle='~Network(){if(!pulse_lease.Close())';assert s.count(needle)==1
s=s.replace(needle,'~Network(){std::fprintf(stderr,"C512_NPAIR requested=%u calls=%llu\\n",unsigned(C512NPairRequested()),c512_npair_calls);if(!pulse_lease.Close())')
p.write_text(s)
files=['hip/deep_fast.hip','Development/HIP/packed_weights.h','Development/HIP/hip_reference_network.h'];patch=''
for f in files:
 patch+=subprocess.run(['diff','-u','--label','a/'+f,'--label','b/'+f,str(R/f),str(S/f)],capture_output=True,text=True).stdout
(O/'candidate.patch').write_text(patch)
(O/'source-identity.json').write_text(json.dumps({f:{'base':hashlib.sha256((R/f).read_bytes()).hexdigest(),'candidate':hashlib.sha256((S/f).read_bytes()).hexdigest()} for f in files},indent=2))
print(S)
