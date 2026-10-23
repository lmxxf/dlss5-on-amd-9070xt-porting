import pathlib,shutil,subprocess,json
repo=pathlib.Path('/home/lmxxf/work/ai-theorys-study/wechat/assets/297')
work=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2')
original=(repo/'hip/wave_owned_c32.inc').read_text()
needle='q=__builtin_fmaf(absf(g),-.055908203125f,.447265625f),p=__builtin_fmaf(g,q,.89453125f);'
assert original.count(needle)==2
for name,alt in [('mix',0),('mix-alt',1)]:
 root=work/name;shutil.copytree(repo/'hip',root/'hip',dirs_exist_ok=True)
 types=root/'Development/HIP';types.mkdir(parents=True,exist_ok=True);shutil.copy2(repo/'Development/HIP/swin_persistent_types.h',types)
 helper='''// knife2 CPU candidate: two exactly representable half constants, FP32 variable/result.
// Literal low half 0xab28 = -0.055908203125; high half 0x3728 = 0.447265625.
// No MODE change; input g is the existing clamp result, and abs is an input modifier.
DEV float cw_act_first_mix(float g){float q;asm("v_fma_mix_f32 %0, %2, |%1|, %2 op_sel:[0,0,1] op_sel_hi:[1,0,1]" : "=v"(q) : "v"(g), "s"(0x3728ab28u));return q;}
'''
 s=original.replace('// Two independent RTZ conversions',helper+'// Two independent RTZ conversions',1)
 second='p=__builtin_fmaf(g,q,.89453125f);' if not alt else 'p=(e&1u)?__builtin_fmaf(q,g,.89453125f):__builtin_fmaf(g,q,.89453125f);'
 s=s.replace(needle,'q=cw_act_first_mix(g),'+second)
 (root/'hip/wave_owned_c32.inc').write_text(s)
 patch=subprocess.run(['diff','-u','--label','a/hip/wave_owned_c32.inc','--label','b/hip/wave_owned_c32.inc',str(repo/'hip/wave_owned_c32.inc'),str(root/'hip/wave_owned_c32.inc')],capture_output=True,text=True).stdout
 (pathlib.Path(__file__).parent/(name+'.patch')).write_text(patch)
 print(root)
