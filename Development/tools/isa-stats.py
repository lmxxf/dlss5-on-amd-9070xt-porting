#!/usr/bin/env python3
"""Static ISA statistics of every pipeline in an RGP capture.
usage: isa-stats.py <capture.rgp> <workdir>   (needs: ssh amd9070 with the Radeon Developer Tool Suite unzipped at RDTS below)
1. extracts every PAL pipeline ELF embedded in the .rgp (RDF container) into <workdir>/elf/pNNNN.elf
2. runs `rga.exe -s bin --co` on the AMD box for each (ISA + stats csv), copies them back to <workdir>/isa/
3. prints one row per pipeline: isa bytes, scratch (spill) bytes, vgpr, lds, instruction/wmma/lds/load/store/wait/cvt counts,
   and tags from literal constants found in the code (ffn_poly = C32 FFN activation, c32exp = C32 softmax bit mapping,
   gauss2pi = preblock noise, ...) — the cso files carry a placeholder DXBC hash (preview dxc does not sign), so this is
   the only way to tell which pipeline is which."""
import sys,os,struct,glob,csv,subprocess,re,collections
RDTS=r'C:\Users\lmxxf\Downloads\RadeonDeveloperToolSuite-2026-05-28-1806\RadeonDeveloperToolSuite-2026-05-28-1806'
rgp,work=sys.argv[1],sys.argv[2];os.makedirs(f'{work}/elf',exist_ok=True);os.makedirs(f'{work}/isa',exist_ok=True)
d=open(rgp,'rb').read();i=0;n=0
while True:
 i=d.find(b'\x7fELF',i)
 if i<0:break
 if d[i+4:i+7]!=b'\x02\x01\x01':i+=4;continue
 shoff=struct.unpack_from('<Q',d,i+0x28)[0];shent,shnum=struct.unpack_from('<HH',d,i+0x3a);end=shoff+shent*shnum
 if not shoff or i+end>len(d) or end>20_000_000 or shent!=64:i+=4;continue
 open(f'{work}/elf/p{n:04d}.elf','wb').write(d[i:i+end]);n+=1;i+=end
print('pipelines',n)
subprocess.run(f'cd {work}/elf && tar czf ../elfs.tgz p*.elf && scp -q ../elfs.tgz amd9070:D:/DLSSNR-Lab/logs/rga/',shell=True,check=True)
subprocess.run(['ssh','amd9070',f'cd /d D:\\DLSSNR-Lab\\logs\\rga && tar xzf elfs.tgz && cd /d "{RDTS}" && for %f in (D:\\DLSSNR-Lab\\logs\\rga\\p*.elf) do @rga.exe -s bin --co %f --isa D:\\DLSSNR-Lab\\logs\\rga\\%~nf.isa -a D:\\DLSSNR-Lab\\logs\\rga\\%~nf.csv >nul 2>&1'],check=True)
subprocess.run(f'ssh amd9070 "cd /d D:\\DLSSNR-Lab\\logs\\rga && tar czf isa.tgz gfx1201_p*.isa gfx1201_p*.csv" && scp -q amd9070:D:/DLSSNR-Lab/logs/rga/isa.tgz {work}/isa/ && cd {work}/isa && tar xzf isa.tgz',shell=True,check=True)
sig={'ffn_poly':['0xbd650000','0x3ee50000','0x3f650000'],'c32exp':['0x3d380000','0x3fa68000'],'gauss2pi':['0x40c90fdb'],'e4m3_512':['0x44000000']}
rows=[]
for f in sorted(glob.glob(f'{work}/isa/gfx1201_p*_comp.isa')):
 t=open(f).read().lower();name=os.path.basename(f)[8:13];st=list(csv.reader(open(f.replace('.isa','.csv'))))[1]
 tags=[k for k,v in sig.items() if all(x in t for x in v)]
 ninst=sum(1 for l in t.splitlines() if l.strip() and not l.strip().startswith(('//','label','.')))
 rows.append((name,int(st[15]),st[1],st[10],st[5],ninst,t.count('wmma'),t.count('ds_'),t.count('global_load')+t.count('buffer_load'),t.count('global_store')+t.count('buffer_store'),t.count('s_wait'),t.count('v_cvt'),t.count('scratch_'),','.join(tags)))
rows.sort(key=lambda r:-r[1])
print('p     isa    scr vgpr  lds   inst wmma  ds   ld  st wait  cvt scr# tags')
for r in rows:print('%s %6d %4s %4s %5s %6d %4d %4d %4d %3d %4d %4d %4d %s'%r)
