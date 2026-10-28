from pathlib import Path
import json,hashlib,ntpath
R=Path.cwd();P=R/'Development/results/isa-asm-phase3-20261029';old=R/'Development/results/gap-phase3-20261023/current-gap';remote=r'D:\DLSSNR-Lab\isa-asm-phase3-20261029';mz=r'D:\DLSSNR-Lab\competitor-timing-20260930\mz';fx=r'D:\DLSSNR-Lab\sync-network-gap1080-20261006';assets=r'D:\DLSSNR-Lab\history-trial-041a-20261006\assets';files=[]
for f in json.loads((old/'evidence/mochi-asset-hashes.json').read_text(encoding='utf-8-sig')):files.append({'path':ntpath.join(mz,f['path']),'sha256':f['sha256']})
files+=[{'path':ntpath.join(mz,'nr_graph.exe'),'sha256':'8ad3ac1cfd5d83f58fb9a223e21d7a970118518839bada3abcaba66af5e8d153'},{'path':ntpath.join(fx,'fixture/processing1088.rgba32f'),'sha256':'3ef42d34cebd1bc847e5caa3a95b3c3b70f45c9eeed4e828a3e2f85615b49b68'},{'path':ntpath.join(fx,'fixture/valid.rgba32f'),'sha256':'17b2e09bf9757a5a98fbd65a08f618100ec42f4d7abb7036cb03cb19e3ae4e2a'},{'path':ntpath.join(fx,'fast1-1088.flags'),'sha256':'e41916ae81d138a424ce3da8f174034f3af9c6783bdb35909eadbffe22eadcdb'},{'path':remote+r'\payload\pure.exe','sha256':hashlib.sha256((P/'payload/pure.exe').read_bytes()).hexdigest()}]
for n,h in (l.split()[::-1] for l in (R/'hip/SHA256SUMS').read_text().splitlines()):
 if n.startswith('gfx1201/'):files.append({'path':ntpath.join(remote,'payload','HIP',n.replace('/','\\')),'sha256':h})
jobs=[]
for rnd in range(1,4):
 for slot,kind in enumerate(['N','M','M','N']):
  label=f'gap/r{rnd}-{slot}-{kind}'
  if kind=='N':exe=remote+r'\payload\pure.exe';args=f'"{assets}" "{remote}\\payload\\HIP\\gfx1201" "{fx}\\fast1-1088.flags" "{fx}\\fixture\\processing1088.rgba32f" "{{OUT}}" 80 160 1088';gold='153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1'
  else:exe=mz+r'\nr_graph.exe';args=f'--plan {mz}\\plan-1920x1080.txt --model-pack {mz}\\dlssnr.bin --spv-dir {mz}\\spv --host-boundary --reuse --source-width 1920 --source-height 1080 --accumulation fp32 --pipeline-cache {remote}\\mochi-cache.bin --warmup 80 --repeats 160 --chunk 1 --style 1 --img-seed 0 --in-image {fx}\\fixture\\valid.rgba32f --out-image {{OUT}}\\final.rgba32f';gold='624d9d1a2f5cedb300182bb7766df1c3b421d57555c9edc20220baa14431a612'
  jobs.append({'label':label,'exe':exe,'arguments':args,'env':{},'gold':gold,'round':rnd,'slot':slot,'kind':kind})
(P/'gap-jobs.json').write_text(json.dumps({'name':'locked-d1185d2-current-NMMN','files':files,'jobs':jobs},indent=2))
