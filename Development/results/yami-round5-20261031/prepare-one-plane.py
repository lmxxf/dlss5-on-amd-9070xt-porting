from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'one-plane/source';shutil.copytree(p/'r2d-both/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_mh.inc';s=f.read_text()
a=' __attribute__((shared)) unsigned char plane0[64*C],plane1[64*C];';assert s.count(a)==1
s=s.replace(a,''' // R5: stage phase outputs in registers and reuse one LDS plane after uniform barriers.
 constexpr bool w2_one_plane=C==128&&!Up&&!W2_EXPLICIT_WINDOW&&!W2_DEFER_Q&&!W2_PHASE_REP;
 __attribute__((shared)) unsigned char plane0[64*C],plane1_backing[64*C];
 auto &plane1=w2_one_plane?plane0:plane1_backing;
 i2 one_ffn[4][2],one_mix[4][2];''')
start=s.index(' // Each head contracts only its own 128 hidden channels.');end=s.index(' // Cross-head FFN mixing plus the original residual.',start)
piece=s[start:end];a='w2_store<C>(plane1,qb+qi,head*2+ci,a);';assert piece.count(a)==1;piece=piece.replace(a,'if constexpr(w2_one_plane)one_ffn[qb+qi][ci]=a;else '+a)
a='w2_store<C>(plane1,qt,head*2+ci,a);';assert piece.count(a)==3;piece=piece.replace(a,'if constexpr(w2_one_plane)one_ffn[qt][ci]=a;else '+a)
a='W2_REP_END\n w2_sync();\nW2_REP_BEGIN(2)';assert piece.count(a)==1;piece=piece.replace(a,'''W2_REP_END
 if constexpr(w2_one_plane){w2_sync();_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)w2_store<C>(plane0,qt,head*2+ci,one_ffn[qt][ci]);}
 w2_sync();
W2_REP_BEGIN(2)''');s=s[:start]+piece+s[end:]
start=s.index(' // Cross-head FFN mixing plus the original residual.');end=s.index(' // Q/K use A-fragment orientation.',start);piece=s[start:end];a='w2_store<C>(plane0,qt,head*2+ci,a);';assert piece.count(a)==1;piece=piece.replace(a,'if constexpr(w2_one_plane)one_mix[qt][ci]=a;else '+a)
a='W2_REP_END\n w2_sync();';assert piece.count(a)==1;piece=piece.replace(a,'''W2_REP_END
 if constexpr(w2_one_plane){w2_sync();_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)w2_store<C>(plane0,qt,head*2+ci,one_mix[qt][ci]);}
 w2_sync();''');s=s[:start]+piece+s[end:]
a=' constexpr bool W2SkipB8=SkipByteOut;';assert s.count(a)==1;s=s.replace(a,''' i2 one_residual[4][2];
 if constexpr(w2_one_plane){_Pragma("unroll") for(uint qt=0;qt<4;qt++)_Pragma("unroll") for(uint ci=0;ci<2;ci++)one_residual[qt][ci]=w2_load<C>(plane0,qt,head*2+ci);w2_sync();}
'''+a)
a='   a=w2_load<C>(plane0,qt,ct);';assert s.count(a)==2;s=s.replace(a,'''#if !defined(W2_ATTENTION_ONLY)
   if constexpr(w2_one_plane)a=one_residual[qt][ci];else
#endif
'''+a)
f.write_text(s)
(p/'one-plane-plan.md').write_text('C128 occupancy hypothesis, prepared during independent3arm run\n\nCurrentstockC128:16384B LDS,128threads,242VGPR. ReduceLDStoone8192Bplane without changing mathematical operations: retainFFN packedresults untilallinputreadsfinish;barrierthenoverwrite;retaincrossheadmixpackedresultsuntilallFFNreadsfinish;barrierthenoverwrite;afterQKV retainownedresidualfragments inregisters andbarrierbeforeAVoverwritesLDS. Finalprojectionreadsownresidualfromregisters. NoUp/explicitSP/deferredQ/phase-repeat paths. Additionalbarriersareuniform. InspectactualLDS/VGPR/privatefirst,querydriveroccupancy,thenGold7 andsamebatchtiming. Noassumptionthatpaperregistercountsraiseoccupancy.\n')
