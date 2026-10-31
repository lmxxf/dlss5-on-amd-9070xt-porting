from pathlib import Path
import json,re,shutil,subprocess,hashlib
r=Path.cwd();o=r/'Development/releases/0.46';old=o.parent/'0.45';p=r/'Development/results/yami-round5-20261031/payload'
zh='''## 0.46 · 2026-10-31

以2f19eebc及本次发行文档快照为基础，采用Round5最终双架构80模块和已验证O2宿主。自0.45以来纳入Backfeed刀1的限定形状优化、C32 Phase 5B、K-Search Round1–4的已采纳无损路径，以及Round5仅norm900启用的带精确性保护四路dot2分母归约；失败候选不入包。保持Small 0.5x、MODE/AE和F9默认语义。0 bit-diff指各轮对应基线、已验证输入及配置，不表示跨模式或历史版本任意输出恒等，也未证明性能上限。

离线单帧耗时（不同验证批次范围，非游戏FPS）：Small 0.5x：1080档5.311–5.363ms、900档3.937–3.947ms、720档2.742ms、小窗1.680ms；Original 1x：1088档8.335–8.425ms（纯NN7.984ms）、1152档8.859–8.945ms、960档6.243ms。Round5同批Original960 mean下降25.60µs、p99下降30.55µs，三轮全胜；最终20配置138对文件0 bit-diff。数据不构成所有七档的同批区间保证。

三包与SHA回执：Development/releases/0.46/；双游戏备份安装回执：Development/results/game-install-20261031/。公开网盘尚未上传；0.45下载链接仍对应0.45。
'''
en='''## 0.46 · 2026-10-31

Based on 2f19eebc plus the release-document snapshot. Ships the validated Round5 dual-architecture 80 modules and O2 hosts. Since 0.45: accepted shape-limited Backfeed knife1, C32 Phase5B, accepted K-Search Rounds1–4, and Round5 guarded four-accumulator dot2 denominator reduction in norm900 only. Rejected candidates are excluded. Small0.5x, MODE/AE and F9 defaults remain. Bit-exact evidence applies to the tested inputs/configurations against each round's baseline; no universal cross-mode equivalence or performance ceiling is claimed.

Offline single-frame times across validation batches (not game FPS): Small0.5x 1080:5.311–5.363ms, 900:3.937–3.947ms, 720:2.742ms, window:1.680ms; Original1x 1088:8.335–8.425ms (pure NN7.984ms), 1152:8.859–8.945ms, 960:6.243ms. Round5 same-batch Original960 mean−25.60µs/p99−30.55µs, all three rounds improved; final20 configurations/138 file pairs bit-identical. These ranges are not a same-batch guarantee across all seven tiers.

Receipts: Development/releases/0.46/ and Development/results/game-install-20261031/. Public mirrors have not been updated; existing0.45 links remain0.45.
'''
for f,t in [('CHANGELOG.zh-CN.md',zh),('CHANGELOG.md',en)]:
 x=r/f;x.write_text(x.read_text()+'\n'+t)
for f,t,pat in [('README.zh-CN.md',zh,r'^\*\*当前版本：0\.45.*$'),('README.md',en,r'^\*\*Current release: 0\.45.*$')]:
 x=r/f;x.write_text(re.sub(pat,lambda m:t,x.read_text(),flags=re.M))
x=r/'Development/WorkingPlan.md';x.write_text(x.read_text()+'\n'+zh)
receipt=json.loads((old/'receipt.json').read_text());s=(old/'package-045.ps1').read_text(encoding='utf-8-sig').replace('0.45','0.46').replace('release-045','release-046').replace("base='0.44'","base='0.45'")
for var,name in [('regularSha','dlss5-amd.addon64'),('runtimeSha','LmxxfNrRuntime.dll')]:s=re.sub(r'\$'+var+r"='[a-f0-9]+'",lambda m:'$'+var+"='"+hashlib.sha256((p/name).read_bytes()).hexdigest()+"'",s)
s=re.sub(r'\$baselineHashes=@\{[^\n]+',lambda m:'$baselineHashes=@{'+ ';'.join("'"+v['name'].removesuffix('-0.45')+"'='"+v['sha256']+"'"for v in receipt['packages'])+'}',s)
s=s.replace('gap-phase6-20261023/final-build','yami-round5-20261031').replace('Frozen phase6 payload; limited QK three shapes; Up pair STOP; Small/default/F9 semantics preserved','Round5 final norm900 payload; accepted Backfeed/Phase5B/KSearch; defaults preserved').replace('See final phase6 validation','See Round5 validation')
(o/'package-046.ps1').write_text(s,encoding='utf-8-sig')
shutil.copy(old/'stage-config-layers.ps1',o)
for n in ['HOST-LICENSE.txt','CORE-LICENSE.txt']:shutil.copy(old/n,o/n)
(o/'SOURCE-README.txt').write_text('0.46 corresponding source: git base2f19eebc plus uncommitted release documentation. Includes unchanged GPL host and updated MIT core/HIP source. See source-refresh.json for archive identity.\n')
subprocess.run(['python3',str(old/'prepare-source.py'),'--out',str(o),'--base','/home/lmxxf/work/dlss5-release-045-build/re9-presr-source.tar.gz','--commit',subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()],check=True)
# installer uses same atomic transaction and complete backup; update pins and explicit gates.
s=(r/'Development/results/game-install-20261024/install.ps1').read_text(encoding='utf-8-sig').replace('20261024','20261031').replace('release-045','release-046').replace('0.45','0.46').replace('0.44','0.45').replace('.045-','.046-').replace('INSTALL_045','INSTALL_046').replace('gap-phase6-20261023/final-build/receipt.json','yami-round5-20261031/final-decision.json')
s=s.replace('40b4021ad162992ab9a4c587035ed1e821a6bd3cca7d92482514aec66218fee9',receipt['packages'][1]['sha256']).replace('0f10b06a73d1b683a7d95e0e3174d6c2cca1ff43c9d215a0929bf934f042927f',receipt['packages'][2]['sha256'])
s=s.replace('-Raw|ConvertFrom-Json','-Raw -Encoding UTF8|ConvertFrom-Json')
s=s.replace('$protected=Configs;Save',"$protected=Configs;if($protected.Count -ne 16){throw 'Expected16 protected configurations'};Save")
s=s.replace('CheckConfigs;$smokes=@()',"if($installed.Count -ne 169){throw 'Expected169 installed files'};CheckConfigs;$smokes=@()")
(r/'Development/results/game-install-20261031/install.ps1').write_text(s,encoding='utf-8-sig')
(o/'baseline-045.json').write_text(json.dumps(receipt))
print('Prepared documentation, source archive and scripts')
