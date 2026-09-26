// fp8-sat-mode probe: what v_cvt_pk_fp8_f32 and v_cvt_f16_f32 return with and without MODE.FP16_OVFL (hwreg MODE bit 23).
#define KERNEL extern "C" __attribute__((global))
typedef unsigned int uint;
static __attribute__((device)) uint rd_mode(){return __builtin_amdgcn_s_getreg(1|(0<<6)|(31<<11));}
KERNEL void probe(const float*in,uint*out,uint n,uint mode_bit){
 uint i=__builtin_amdgcn_workitem_id_x();
 if(mode_bit)__builtin_amdgcn_s_setreg(1|(23<<6)|(0<<11),1);
 if(i>=n)return;float x=in[i];
 uint clamped=uint(__builtin_amdgcn_cvt_pk_fp8_f32(__builtin_fminf(__builtin_fmaxf(x,-448.f),448.f),0.f,0,false))&255u;
 uint raw=uint(__builtin_amdgcn_cvt_pk_fp8_f32(x,0.f,0,false))&255u;
 _Float16 h=(_Float16)x;unsigned short hb=__builtin_bit_cast(unsigned short,h);
 out[i*4+0]=clamped;out[i*4+1]=raw;out[i*4+2]=hb;out[i*4+3]=rd_mode();}
