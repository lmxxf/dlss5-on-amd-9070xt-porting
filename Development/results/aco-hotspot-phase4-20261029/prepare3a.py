from pathlib import Path
import shutil,difflib,json
p=Path(__file__).parent;dst=p/'work/3a-source';assert not dst.exists();shutil.copytree('hip',dst,ignore=shutil.ignore_patterns('*.hsaco','*.exe','*.dll'));f=dst/'wave_owned_mh.inc';old=f.read_text();s='#ifndef P4_DEEP_PREFETCH\n#define P4_DEEP_PREFETCH 0\n#endif\n'+old
needle='  _Pragma("clang loop unroll(disable)") for(uint ht=0;ht<8;ht+=T){\n   f8 expanded[B][T]{};';assert s.count(needle)==1
rep='''  constexpr bool audit_prefetch=P4_DEEP_PREFETCH && C==128 && ByteIn && ByteOut && !Up && !W16 && !HalfOut && !SkipHalf && !SkipByte && !SkipByteOut
#if W2_EXPLICIT_WINDOW
   && !ExplicitWindow
#endif
   ;
  _Pragma("clang loop unroll(disable)") for(uint ht=0;ht<8;ht+=T){
   i2 prefetched_contract[T][2];
   if constexpr(audit_prefetch){
    _Pragma("unroll") for(uint tile=0;tile<T;tile++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)
     prefetched_contract[tile][ci]=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);
    __builtin_amdgcn_sched_barrier(0);
   }
   f8 expanded[B][T]{};''';s=s.replace(needle,rep)
needle='     i2 b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);';assert s.count(needle)==2;s=s.replace(needle,'     i2 b;if constexpr(audit_prefetch)b=prefetched_contract[tile][ci];else b=w2_weight(fw+4*C*C,head*32+ci*16+rc,4*C,head*128+(ht+tile)*16);',1);f.write_text(s)
(p/'3a-source.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/hip/wave_owned_mh.inc',tofile='b/hip/wave_owned_mh.inc')))
f=dst/'build-modules.ps1';s=f.read_text();key="@{ name = 'c64-wave2-fast'; defines = @(";assert s.count(key)==1;s=s.replace(key,key+"'P4_DEEP_PREFETCH 1',");f.write_text(s)
(p/'3a-design.json').write_text(json.dumps({'target':'c128_wave2_bi_bo','B':4,'T':2,'preload_words':8,'preload_bytes_per_lane':32,'scope':'Only C128 byte-in/out, non-Up, non-W16, full output, non-explicit-window specialization','waves_per_eu':'stock HIP_KERNEL_WPE=0 already unrestricted; no fictitious removed constraint','mathematics':'Same kt/ht/tile/ci/qi order and activation/rounding; only immutable contract weight loads advanced before expand','resource_policy':'0 spill/private required; register growth permitted for this residency/prefetch test; no extra LDS'},indent=2))

shared=dst.parent/"Development/HIP";shared.mkdir(parents=True,exist_ok=True);shutil.copyfile("Development/HIP/swin_persistent_types.h",shared/"swin_persistent_types.h")
