from pathlib import Path
import shutil
p=Path(__file__).parent;root=p/'sp128/source';shutil.copytree('src',root/'src',dirs_exist_ok=True);d=root/'Development/HIP';d.mkdir(parents=True,exist_ok=True)
for f in Path('Development/HIP').glob('*.h'):shutil.copyfile(f,d/f.name)
shutil.copyfile('Development/HIP/benchmark_live_capture.cpp',d/'benchmark_live_capture.cpp')
f=d/'swin_persistent_network.h';s=f.read_text().replace('struct SpPlan {','struct SpPlan {\n std::string module_key="sp";')
s=s.replace('bool SpEnabled(U c,bool decoder){','bool R5Sp128()const{const char*v=std::getenv("NR_R5_SP128");return v&&std::strcmp(v,"1")==0&&!small_active&&fast_numeric&&multi_pass==1&&W==1920&&(H==1088||H==1152);}\nbool SpEnabled(U c,bool decoder){')
s=s.replace('SpEnv("SP_CHANNELS",SwinRunCompatible(opt)?4u:0u)','SpEnv("SP_CHANNELS",(SwinRunCompatible(opt)?4u:0u)|(R5Sp128()?2u:0u))')
s=s.replace('SpPlan plan;plan.offsets.push_back(0);','SpPlan plan;plan.module_key=(c==128&&R5Sp128())?"sp128fast":"sp";plan.offsets.push_back(0);')
old='if(!modules.count("sp")){Handle m{};api.Check(api.LoadModule(&m,(opt.modules+"/swin-persistent.hsaco").c_str()),"SP module");modules["sp"]=m;}'
new='if(!modules.count(plan.module_key)){Handle m{};const char*file=plan.module_key=="sp128fast"?"/swin-persistent-fast.hsaco":"/swin-persistent.hsaco";api.Check(api.LoadModule(&m,(opt.modules+file).c_str()),"SP module");modules[plan.module_key]=m;}'
assert old in s;s=s.replace(old,new).replace('HasFn("sp",','HasFn(plan.module_key,').replace('Fn("sp",','Fn(plan.module_key,');f.write_text(s)
f=d/'hip_reference_network.h';s=f.read_text();anchor='if(wave_owned_active)HasFn("c32_wave1","c32_wave1_post_b8_rgba");';assert s.count(anchor)==1
s=s.replace(anchor,'if(R5Sp128()){SpGetPlan(W/8,H/8,128,10,4);SpGetPlan(W/8,H/8,128,57,4);HasFn("sp128fast","sp_init");HasFn("sp128fast","sp_run128");HasFn("sp128fast","sp_recover128");}\n'+anchor);f.write_text(s)
