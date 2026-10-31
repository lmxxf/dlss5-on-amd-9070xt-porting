using U=unsigned;
extern "C" __attribute__((amdgpu_kernel)) void old_vit(const U*in,U*out){U i=__builtin_amdgcn_workitem_id_x();U a=in[2*i],b=in[2*i+1];U x=((((a>>13)-0x1c000u)<<4)+0x4000u)&65535u;U y=((((b>>13)-0x1c000u)<<4)+0x4000u)&65535u;out[i]=x|(y<<16);}
extern "C" __attribute__((amdgpu_kernel)) void pair_vit(const U*in,U*out){U i=__builtin_amdgcn_workitem_id_x();U a=in[2*i],b=in[2*i+1];out[i]=(__builtin_amdgcn_perm(b,a,0x06050201u)>>1)&0x3ff03ff0u;}
