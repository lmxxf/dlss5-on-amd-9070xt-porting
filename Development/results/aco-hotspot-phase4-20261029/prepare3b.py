from pathlib import Path
import re,json,difflib,subprocess
p=Path(__file__).parent;w=p/'work/3b';w.mkdir(parents=True,exist_ok=True);src=p/'work/c64-baseline/gfx1201/c64-wave2-fast.hsaco.s';full=src.read_text();name='c128_wave2_bi_bo';start=full.index(name+':');end=full.index('.Lfunc_end',start);body=full[start:end];lines=body.splitlines(True)
def regs(t):
 out=set()
 for m in re.finditer(r'\b([vs])(?:\[(\d+):(\d+)\]|(\d+))',t):
  out.update(m[1]+str(n)for n in(range(int(m[2]),int(m[3])+1)if m[2]else[int(m[4])]))
 return out
ins=[];labels={};pending_labels=[]
for ln,l in enumerate(lines):
 q=l.strip()
 if q.endswith(':')or re.match(r'^\.L\w+:',q):pending_labels.append(q.split(':')[0]);continue
 m=re.match(r'^([a-z][a-z0-9_]*)\s*(.*)',q)
 if not m or q.startswith((';','.')):continue
 for lab in pending_labels:labels[lab]=len(ins)
 pending_labels=[];ins.append({'line':ln,'op':m[1],'args':m[2],'text':q})
N=len(ins);succ=[]
for i,x in enumerate(ins):
 op=x['op'];ss=[]if op=='s_endpgm'else([i+1]if i+1<N else[])
 if op=='s_branch'or op.startswith('s_cbranch'):
  lab=x['args'].split()[0];assert lab in labels,(op,lab)
  if op=='s_branch':ss=[]
  ss.append(labels[lab])
 assert op not in ['s_setpc_b64','s_swappc_b64'],'unmodelled indirect control flow'
 succ.append(ss)
preds=[[]for _ in ins]
for i,ss in enumerate(succ):
 for j in ss:preds[j].append(i)
# Conservative forward may-pending-write dataflow, including loop backedges.
states=[(set(),set(),set())for _ in ins];outs=[(set(),set(),set())for _ in ins];changed=True;iters=0
while changed:
 changed=False;iters+=1
 for i,x in enumerate(ins):
  st=tuple(set().union(*(outs[k][j]for k in preds[i]))if preds[i]else set()for j in range(3));out=tuple(set(z)for z in st);op=x['op'];arg=x['args'];dest=regs(arg.split(',',1)[0])
  if op.startswith(('global_load','buffer_load','flat_load','scratch_load')):out[0].update(dest)
  if op.startswith(('ds_load','ds_read','ds_bpermute','ds_permute')):out[1].update(dest)
  if op.startswith(('s_load','s_buffer_load')):out[2].update(dest)
  for j,wait in enumerate(['s_wait_loadcnt','s_wait_dscnt','s_wait_kmcnt']):
   if op==wait and int(arg.split()[0],0)==0:out[j].clear()
  if st!=states[i]or out!=outs[i]:states[i]=st;outs[i]=out;changed=True
 assert iters<100,'pending-write fixed point failed'
def targets(order):
 out={}
 for pos,idx in enumerate(order):
  x=ins[idx]
  if x['op']!='s_delay_alu':continue
  a=x['args'];t=[order[pos+1]]
  if 'instid1('in a:
   m=re.search(r'instskip\((SAME|NEXT|SKIP_[1-4])\)',a);v=0 if not m or m[1]=='SAME'else 1 if m[1]=='NEXT'else int(m[1][-1])+1;t.append(order[pos+v+1])
  out[idx]=t
 return out
old_targets=targets(list(range(N)));candidates=[]
from collections import Counter
reasons=Counter();windows=[]
allowed_s={'s_delay_alu','s_mov_b32','s_mov_b64','s_add_u32','s_addc_u32','s_lshl_b32','s_lshr_b32','s_and_b32','s_or_b32','s_xor_b32','s_add_nc_u64'}
for i,x in enumerate(ins):
 if not x['op'].startswith('global_load_'):continue
 reasons['global_loads']+=1
 dest=regs(x['args'].split(',',1)[0]);address=regs(x['args'].split(',',1)[1]);clause=None
 if i and ins[i-1]['op']=='s_clause':
  clause=i-1;count=int(ins[clause]['args'],0)
  if count==0:continue
  stop=i-1
 else:continue  # Keep the original implicit VALU wait through the next VMEM in the old hard clause.
 if i+1>=N or not ins[i+1]['op'].startswith(('global_load','buffer_load','flat_load')):continue
 earliest=stop
 for j in range(stop-1,max(-1,stop-49),-1):
  y=ins[j];op=y['op'];raw=y['text'];between=''.join(lines[y['line']:x['line']])
  if any(re.match(r'\s*\.L\w+:',l)for l in lines[y['line']+1:x['line']]):break
  if ('exec'in raw or'movrel'in op or'cmpx'in op):break
  if not(op.startswith('v_')or op in allowed_s or(op=='s_setreg_imm32_b32'and'HW_REG_MODE, 23, 1'in raw)):break
  if regs(raw)&(dest|address):break
  earliest=j
 windows.append({'i':i,'max_cross':stop-earliest,'load':x['text']})
 for j in range(earliest,stop):
  if stop-j<1:continue
  reasons['at_least_one']+=1
  if any((states[j][k]&(dest|address))for k in range(3)):continue
  reasons['pending_safe']+=1
  # Preserve a conservative readiness gap to all prior explicit references/definitions.
  recent_dest_refs=set()
  recent_defs=set()
  for z in ins[max(0,j-8):j]:
   if z['op'].startswith(('v_','s_mov','s_add','s_lsh','s_lshr','s_and','s_or','s_xor','s_cvt','s_mul')):
    for part in z['text'].split('::'):
     pp=part.strip().split(None,1)
     if len(pp)==2:recent_defs|=regs(pp[1].split(',',1)[0])
  if recent_defs&{r for r in address if r.startswith('s')}:continue
  reasons['def_gap_safe']+=1
  order=list(range(N));order.remove(i);order.insert(j,i)
  if targets(order)!=old_targets:continue
  # No VMEM/LDS/SMEM, barriers, branches or EXEC changes crossed; all memory issue order stays identical.
  moved=[z for z in order if ins[z]['op'].startswith(('global_','buffer_','flat_','ds_','s_load','s_buffer_load'))]
  original=[z for z in range(N)if ins[z]['op'].startswith(('global_','buffer_','flat_','ds_','s_load','s_buffer_load'))]
  assert moved==original
  score=(1000 if 's[10:11]'in x['args']else 0)+(stop-j)
  candidates.append((score,i,j,clause,order))
(p/'3b-screen.json').write_text(json.dumps({'counts':dict(reasons),'windows':sorted(windows,key=lambda x:x['max_cross'],reverse=True)[:12]},indent=2))
assert candidates,'no load motion satisfies all conservative safety checks'
_,i,j,clause,order=max(candidates,key=lambda x:x[0]);x=ins[i];newlines=list(lines);newlines[x['line']]=''
if clause is not None:newlines[ins[clause]['line']]='\ts_clause '+hex(int(ins[clause]['args'],0)-1)+'\n'
newlines[ins[j]['line']]=lines[x['line']]+newlines[ins[j]['line']];patched=full[:start]+''.join(newlines)+full[end:];(w/'patched.s').write_text(patched)
(p/'3b-asm.patch').write_text(''.join(difflib.unified_diff(full.splitlines(True),patched.splitlines(True),fromfile='a/c64-wave2-fast.hsaco.s',tofile='b/c64-wave2-fast.hsaco.s',n=12)))
proof={'kernel':name,'old_function_line':x['line']+1,'new_function_line':ins[j]['line']+1,'load':x['text'],'crossed':[z['text']for z in ins[j:i]],'destination_registers':sorted(regs(x['args'].split(',',1)[0])),'address_registers':sorted(regs(x['args'].split(',',1)[1])),'cfg_nodes':N,'dataflow_iterations':iters,'pending_global_lds_scalar_writes_at_new_position':[sorted(z)for z in states[j]],'no_conflicting_pending_write':True,'no_operand_read_or_write_in_advance_interval':True,'no_exec_or_lane_indirection_change':True,'no_branch_or_label_crossed':True,'backedges_included':True,'all_delay_alu_target_instruction_ids_preserved':True,'all_memory_issue_order_preserved':True,'all_wait_count_instructions_unchanged':True,'read_only_proof':'C128 non-Up body loads only const input/fw/aw. Host Body keeps input Tensor live and allocates distinct output; fw/aw immutable. No concurrent game/other GPU workload, normal Wave2 launch uses ordered stream.','register_allocation':'No rename/new registers; existing destination dead in moved interval, No operand reference in advance interval and no pending asynchronous writers. VMEM implicitly waits VA_VDST=0 (LLVM23 AMDGPUInsertDelayAlu::instructionWaitsForVALU, FLAT flag); next untouched VMEM in original hard clause retains the old implicit VALU completion boundary. SGPR address has no recent VALU definition., no pending async writers from any CFG predecessor.','clause_adjustment':None if clause is None else {'old':ins[clause]['text'],'new_count':int(ins[clause]['args'],0)-1},'implicit_valu_wait_preserved_by_next_old_site_vmem':ins[i+1]['text'],'llvm_semantic_reference':'/home/lmxxf/work/llvm-src-23/llvm/lib/Target/AMDGPU/AMDGPUInsertDelayAlu.cpp:36; FLATInstructions.td isFLAT=1 for global operations','metadata_text_untouched':full[end:]==patched[patched.index('.Lfunc_end',patched.index(name+':')):]}
(p/'3b-safety-proof.json').write_text(json.dumps(proof,indent=2));print(json.dumps({k:v for k,v in proof.items()if k not in ['pending_global_lds_scalar_writes_at_new_position','crossed']},indent=2))
for label,source in [('roundtrip',src),('candidate',w/'patched.s')]:
 obj=w/(label+'.o');out=w/(label+'.hsaco');subprocess.run(['/home/lmxxf/work/llvm-build-23/bin/clang','-cc1as','-triple','amdgcn-amd-amdhsa','-target-cpu','gfx1201','-target-feature','-real-true16','-filetype','obj',str(source),'-o',str(obj)],check=True);subprocess.run(['/home/lmxxf/work/llvm-build-23/bin/ld.lld','--no-undefined','-shared','-plugin-opt=mcpu=gfx1201',str(obj),'-o',str(out)],check=True)
# Preserve the stock ELF container and all non-target functions. Full relinking may change unrelated relocations.
import struct,hashlib
stock=p/'work/c64-baseline/gfx1201/c64-wave2-fast.hsaco'
def function_region(blob,symbol):
 shoff=struct.unpack_from('<Q',blob,40)[0];n=struct.unpack_from('<H',blob,60)[0];sh=[struct.unpack_from('<IIQQQQIIQQ',blob,shoff+64*i)for i in range(n)]
 for sec in sh:
  if sec[1]!=2:continue
  st=sh[sec[6]];strings=blob[st[4]:st[4]+st[5]]
  for k in range(sec[4],sec[4]+sec[5],sec[9]):
   no,info,other,idx,addr,size=struct.unpack_from('<IBBHQQ',blob,k)
   if info&15==2 and strings[no:].split(b'\0')[0].decode()==symbol:
    target=sh[idx];return target[4]+addr-target[3],size
 raise RuntimeError('function symbol missing')
a=stock.read_bytes();b=(w/'roundtrip.hsaco').read_bytes();c=(w/'candidate.hsaco').read_bytes();oa,sa=function_region(a,name);ob,sb=function_region(b,name);oc,sc=function_region(c,name);assert sa==sb==sc;assert a[oa:oa+sa]==b[ob:ob+sb],'target roundtrip not exact'
patched=bytearray(a);patched[oa:oa+sa]=c[oc:oc+sc];assert patched[:oa]==a[:oa]and patched[oa+sa:]==a[oa+sa:];(w/'isolated.hsaco').write_bytes(patched)
proof=json.loads((p/'3b-safety-proof.json').read_text());proof['target_roundtrip_bytes_identical']=True;proof['same_function_size_bytes']=sa;proof['all_bytes_outside_target_function_identical']=True;proof['final_container_method']='cc1as + ld.lld encode target, then same-size function injection into untouched stock ELF to exclude unrelated relink effects';proof['final_sha256']=hashlib.sha256(patched).hexdigest();(p/'3b-safety-proof.json').write_text(json.dumps(proof,indent=2))
