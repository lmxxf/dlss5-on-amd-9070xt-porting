typedef int i2 __attribute__((ext_vector_type(2)));
extern "C" __attribute__((global)) void c512_lds_tr8_support_probe(int* out){
 __attribute__((shared)) i2 tile[32];
 unsigned lane=__builtin_amdgcn_workitem_id_x();
 tile[lane]=(i2){int(lane),int(lane+32)};
 __builtin_amdgcn_s_barrier();
 i2 x=__builtin_amdgcn_ds_load_tr8_b64_v2i32(tile+lane);
 out[lane*2]=x[0];out[lane*2+1]=x[1];
}
