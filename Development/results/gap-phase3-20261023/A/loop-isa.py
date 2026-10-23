import pathlib,re,json,collections
W=pathlib.Path('/home/lmxxf/work/dlss5-gap-phase3-20261023/A');O=pathlib.Path(__file__).parent
res={}
for role,key in [('comgr-baseline','baseline'),('comgr-candidate','candidate')]:
 whole=(W/(role+'-deep_fast-packed-fast.hsaco.objdump.txt')).read_text();name='vit_attention_fused_640_bytein_bout'+('_qt32'if key=='candidate'else'')
 body=re.search(r'<'+name+r'>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',whole,re.S)[1]
 lines=body.splitlines();base=None;inst=[]
 # Raw body starts immediately after function label; recover function base from branch target text and address delta.
 for l in lines:
  a=re.search(r'// ([0-9A-Fa-f]+):',l);op=re.match(r'\s*((?:s_|v_|global_|buffer_|flat_|ds_)[A-Za-z0-9_]+)',l)
  if a and op:inst.append((int(a[1],16),op[1],l))
 # Function has initial loads before first branch; obtain base from whole objdump symbol.
 whole=(W/(role+'-deep_fast-packed-fast.hsaco.objdump.txt')).read_text();name='vit_attention_fused_640_bytein_bout'+('_qt32'if key=='candidate'else'')
 base=int(re.search(r'^([0-9a-f]+) <'+name+'>:',whole,re.M)[1],16)
 loops=[]
 for addr,op,line in inst:
  t=re.search(r'<'+name+r'\+0x([0-9a-f]+)>',line)
  if op.startswith('s_cbranch') and t and (target:=base+int(t[1],16))<addr:
   selected=[(a,o,l)for a,o,l in inst if target<=a<=addr];c=collections.Counter(o for _,o,_ in selected)
   loops.append({'start':hex(target),'end':hex(addr),'instructions':len(selected),'wmma':sum(v for k,v in c.items()if'wmma'in k),'valu':sum(v for k,v in c.items()if k.startswith('v_')and'wmma'not in k),'loads':{k:v for k,v in c.items()if'load'in k and k.startswith(('global_','flat_','buffer_'))},'wait':{k:v for k,v in c.items()if k.startswith('s_wait')},'opcodes':dict(c)})
 res[key]=loops
(O/'loop-isa.json').write_text(json.dumps(res,indent=2));print(json.dumps({k:[{a:b for a,b in d.items()if a!='opcodes'}for d in v]for k,v in res.items()},indent=2))
