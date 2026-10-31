from pathlib import Path
import shutil
p=Path(__file__).parent;r=p/'one-plane-c256-rc/source';shutil.copytree(p/'one-plane-c256/source',r,dirs_exist_ok=True);f=r/'hip/wave_owned_mh.inc';s=f.read_text();start=s.index('DEV void swin_wave2_one_plane_body(');end=s.index('#if !defined(W2_NO_EXPORTS)',start);piece=s[start:end]
a=' const uint win=__builtin_amdgcn_workgroup_id_x(),head=__builtin_amdgcn_workitem_id_x()/32;';assert piece.count(a)==1;piece=piece.replace(a,' const uint win=__builtin_amdgcn_workgroup_id_x();uint head=__builtin_amdgcn_workitem_id_x()/32;')
a=' const uint rc=w2_rc(),gr=w2_gr();';assert piece.count(a)==1;piece=piece.replace(a,' uint rc=w2_rc(),gr=w2_gr();')
a='W2_REP_BEGIN(2)\n';assert piece.count(a)==1;piece=piece.replace(a,a+' uint t256=__builtin_amdgcn_workitem_id_x();__asm__ volatile("":"+v"(t256));rc=t256&15u;\n');f.write_text(s[:start]+piece+s[end:])
(p/'c256-rc-plan.md').write_text('C256OnePlaneW16uses242VGPRandAPIcapacity2despiteLDS32->16KiB;nonW16uses228/API3. Tryphase-boundaryrcrefresh(outsideFFNhtloop)to shortenliverangeandcross<=240allocationthreshold. ThisisadaptedfromACTUALRound3r3c-configout-refresh_rc_onlysource,notthereportdescriptionofreadfirstlane. PreserveoriginalGold/perfdata;inspectresourcebeforeGPU.\n')
