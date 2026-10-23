import pathlib,re,collections,json,subprocess
W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1');O=pathlib.Path(__file__).parent;OB='/home/lmxxf/work/llvm-build-23/bin/llvm-objdump'
r={}
for name in ['vit_attention_fused_640_bytein_bout','vit_attention_fused_640_bytein_bout_vfrag']:
 p=W/'comgr-headmajor/deep_fast-packed-fast.hsaco';text=subprocess.check_output([OB,'-d','--mcpu=gfx1201','--disassemble-symbols='+name,str(p)],text=True)
 (W/(name+'.formal.s')).write_text(text);base=int(re.search(r'^([0-9a-f]+) <'+name+'>:',text,re.M)[1],16);items=[]
 for line in text.splitlines():
  a=re.search(r'// ([0-9A-Fa-f]+):',line);op=re.match(r'\s*((?:s_|v_|ds_|global_|flat_|buffer_)[A-Za-z0-9_]+)',line)
  if a and op:items.append((int(a[1],16),op[1],line))
 loops=[]
 for addr,op,line in items:
  target=re.search(r'<'+name+r'\+0x([0-9a-f]+)>',line)
  if op.startswith('s_cbranch')and target and base+int(target[1],16)<addr:
   start=base+int(target[1],16);ls=[x for x in items if start<=x[0]<=addr];cnt=collections.Counter(x[1]for x in ls)
   if not any('wmma'in k for k in cnt):continue
   loops.append({'start':hex(start),'end':hex(addr),'instructions':len(ls),'wmma':sum(v for k,v in cnt.items()if'wmma'in k),'valu':sum(v for k,v in cnt.items()if k.startswith('v_')and'wmma'not in k),'loads':{k:v for k,v in cnt.items()if'load'in k and k.startswith(('global_','flat_','buffer_'))},'wait':{k:v for k,v in cnt.items()if k.startswith('s_wait')},'opcodes':dict(cnt),'load_lines':[x[2]for x in ls if'load'in x[1]and x[1].startswith(('global_','flat_','buffer_'))]})
 r[name]=loops
print(json.dumps({k:[{a:b for a,b in x.items()if a not in ('opcodes','load_lines')}for x in v]for k,v in r.items()},indent=2));(O/'formal-loop.json').write_text(json.dumps(r,indent=2))
name='vit_stream_qkv_frag_bin_w5f8_vfrag';p=W/'comgr-headmajor/vit-stream-fast.hsaco';text=subprocess.check_output([OB,'-d','--mcpu=gfx1201','--disassemble-symbols='+name,str(p)],text=True);(W/'producer-headmajor.formal.s').write_text(text)
ls=text.splitlines();i=next(i for i,l in enumerate(ls)if'global_store_b128'in l);(O/'producer-vstore-excerpt.s.txt').write_text('\n'.join(ls[i-16:i+6])+'\n');print('producer b128 stores',text.count('global_store_b128'))
