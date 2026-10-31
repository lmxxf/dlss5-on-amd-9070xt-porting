from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'c512-slot/source';shutil.copytree(p/'c512-scope/source',r,dirs_exist_ok=True);f=r/'Development/HIP/hip_reference_network.h';s=f.read_text()
start=s.index(' Handle r5_c512_prob_fn=');end=s.index(' Handle Fn(',start)
helper=''' void InitR5C512Prob(){const char*v=std::getenv("NR_R5_C512_PROB");r5_c512_prob_requested=!v||!*v||std::strcmp(v,"0")!=0;
  if(!r5_c512_prob_requested||W!=1920||H!=1152||!fast_numeric||opt.integration.select_module||!CompactVitTailDeviceEligible())return;
  auto it=functions.find("c512_m32_mh:c512_qkv_attention_compact_w16");if(it==functions.end())return;
  Handle f{};if(api.hipModuleGetFunction(&f,modules.at("c512_m32_mh"),"c512_qkv_attention_compact_w16_prob"))return;
  r5_c512_prob_fn=f;r5_c512_old_fn=it->second;r5_c512_slot=&it->second;
 }
 void SelectR5C512Prob(){if(!r5_c512_prob_fn)return;bool wanted=false;
  if(small_active){const char*ae=std::getenv("DLSS5_VIT_ADAPTIVE");wanted=W==1920&&H==1152&&fast_numeric&&multi_pass==1&&ae&&std::strcmp(ae,"0")==0&&!opt.graph&&!opt.experimental_temporal&&!temporal_feature_tap_active&&!pulse_frame_history&&!native_post_output&&!observer&&opt.dump_dir.empty();}
  if(wanted!=r5_c512_prob_active){*r5_c512_slot=wanted?r5_c512_prob_fn:r5_c512_old_fn;r5_c512_prob_active=wanted;}
 }
'''
s=s[:start]+helper+s[end:]
a='\n  if(r5_c512_prob_active&&m=="c512_m32_mh"&&name=="c512_qkv_attention_compact_w16"){++r5_c512_prob_calls;return r5_c512_prob_fn;}';assert s.count(a)==1;s=s.replace(a,'')
a='  Run("c512_m32_mh",w16_mh?"c512_qkv_attention_compact_w16":"c512_qkv_attention_compact",size_t(ww)*hh/64*16,P(ffn8),att_w,P(av),w,h,ww,hh,sx,sy);';assert s.count(a)==1;s=s.replace(a,a+'\n  if(r5_c512_prob_active&&w16_mh)++r5_c512_prob_calls;')
assert s.endswith('};\n}\n');s=s[:-5]+''' // Appended state preserves the layout/order of every pre-existing member.
 Handle r5_c512_prob_fn=nullptr,r5_c512_old_fn=nullptr;Handle*r5_c512_slot=nullptr;
 bool r5_c512_prob_requested=true,r5_c512_prob_active=false;unsigned long long r5_c512_prob_calls=0;
};
}
'''
base=Path('Development/HIP/hip_reference_network.h').read_text();a=' Handle Fn(';b=' /* DLSS5_FAST_NUMERIC twin';assert s[s.index(a):s.index(b)]==base[base.index(a):base.index(b)];f.write_text(s)
b=(p/'build-c512-scope.sh').read_text().replace('c512-scope','c512-slot');(p/'build-c512-slot.sh').write_text(b)
(p/'c512-slot-plan.md').write_text('Different host implementation after appended-export integration failed: keep the original Fn source unchanged; do not insert a new functions-map key; rebind the already-cached old key only on eligibility transitions. Append state after all existing members, leaving their declaration order intact. Same GPU module as previous scope test. Fixed Small1080 three-ABBA screening before any broader validation; require3/3 mean and p99 improvement. No repeat of the failed identical binary.\n')
