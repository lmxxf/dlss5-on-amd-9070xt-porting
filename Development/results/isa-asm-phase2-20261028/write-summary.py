from pathlib import Path
import json,hashlib
P=Path(__file__).parent;rows={n:json.loads((P/(n+'-statistics.json')).read_text()) for n in ['knife1','knife1-product','knife2','knife3-pool','knife3-split']}
s='''# gfx1201 汇编优化 Phase2：三刀完成，均STOP

执行于2026-10-29，归档沿用任务指定20261028目录。刀1提交57c6533e，刀2提交33a4d791；刀3及总回执在本次提交。**没有任何候选通过完整生产采纳门，生产GPU/宿主源、80模块、hip/SHA256SUMS、游戏与0.45 ZIP均不变。** 不把静态opcode下降或局部收益当成交付收益。

## 验证口径与结果规模

七档：Original1152/1088/900，Small1080/900/720/free896×512。数值以明确生产FAST1 Gold为准（不冒称NV exact0.01）；全部实际上GPU的改动0 bit-diff、invalid0。资源门失败的contract/project没有上GPU，不声称已测Gold。

四档性能：Original1088/900、Small1080/900，各3轮4-slot ABBA，每slot独立进程、严格串行，240帧弃80，960稳态/arm，NET0/edges_only1。新增资格分离与真实产品before/after也各独立3ABBA。总计258帧Gold、216个性能slot，详verification.json。所有远端GPU/RTC作业均经过game-check、进程、>=100GB、原子gpu.lock与15秒看门狗；没有抢占游戏。

## 刀1：C512 V片段直接转置

Q/K保原方向与serial norm，只有V对调WMMA操作数；V写LDS时直接成为AV所需片段。2048元素的地址/覆盖穷尽核对通过。正式COMGR：仅目标核改变，ds_load_u8 64→0，v_lshl_or22→2，指令2104→1970，VGPR188→187，LDS6144不变、零spill。七档Gold通过。

最初四档仅Original1088稳定小幅改善（−4.978µs）。尝试新增_vfrag export并限定该资格，所有旧函数机器码/资源保持不变，新核与实测候选代码逐字同。双架构原型编译并核ELF目标；gfx1200没有实机。最终路由调用计数、默认开关和真实缺导出回退通过。

同最终宿主NR0/1比较平均−5.911µs，但R2 p99+5.393µs；随后将环境读取构造时缓存，以**原生产宿主/模块对最终候选**做真正before/after，三轮mean全部退步，合并+12.284µs/p99+17.925µs。STOP，不继续刷轮，也不归因全部差距到某个未经计数器验证的环节。详knife1-README.md及三批原统计。

## 刀2：ViT expand/contract/project

分别实现交换操作数、连续残差读取、成对量化、b64写出，保原K16/部分和顺序。没有新增LDS。

|子核|静态指令|VGPR|LDS|u8/b8|裁决|
|---|---|---|---|---|---|
|expand|490→407|77→77|0→0|32b8 store→4b64|Gold通过，性能STOP|
|contract|480→372（短pack368）|112→113|4096→4096|32u8/32b8清零|资源门STOP|
|project|424→325（短pack326）|112→115|0→0|32u8/32b8清零|资源门STOP|

contract/project只加测一次明确的短pack生命周期变体，仍113/115；没有用“零LDS”绕过<=112硬门，也未证明不存在更优寄存器实现。转置后lane要处理连续8通道的scale/残差，不是原来单通道跨token的同一活跃变量集合；寄存器是否下降仍须看编译产物。

expand模块中仅目标函数变，七档Gold通过；但Original1088/Small900三轮均值都退，Small1080合并虽好R2反例明显，STOP。详knife2-README.md。

knife2-address-shape.json记录了一个逻辑地址模型：同一16×64 tile始终1024B；32个跨lane合并的b8指令变4个lane内b64，并不保证唯一内存块数或物理事务按8倍减少。模型不是实际硬件cache counter，也不冒称性能变化的唯一因果解释。

## 刀3：pool与split分别验证

pool：只在F8W16/ByteOut/!Gout交换操作数。第一版零填充分支导致额外b64与VGPR增加；将掩码放在成对打包的两个字后，C64 VGPR90→89，C128/C256/C512保持88，SGPR28不变，LDS不增、零spill，每核32b8→4b64。只6个out8导出改变，其他函数保持原代码/资源。

split：用既有Transposed分支处理_w16/_bskip，并将逐元素位拼装改成成对转换，再从相同字节展开回F32残差。两个核VGPR100不变、SGPR17→18、LDS1024→0、零spill；_bskip的32u8清零，32个F32 b32 store变8个b128，字节tile用4个b64。**现生产字节输出本来已经是LDS转置后的2个b128，不把它伪称为还剩32个global_store_b8。** 只这2个导出改变，旧_nof32等变体代码/资源保持不变。

pool的264个宽高/尾块/裁剪地址案例与split24576个tile偏移穷举通过（knife3-layout-proof.json）；两项分别七档42帧Gold通过，各四档3ABBA完成。pool组合无稳定全档净收益；split Original1088三轮mean全退，Small1080三轮p99全退，STOP。没有把两个子项混合后归功或相加，没有声称每个C族的所有未来实现都被否定。

## 同批实测（µs，负数为改善）

|候选/批次|档位|Δmean|Δp99|
|---|---|---:|---:|
'''
for role,d in rows.items():
 for n,v in d['tiers'].items():s+=f"|{role}|{n}|{v['delta_ms']*1000:+.3f}|{v['p99_delta_ms']*1000:+.3f}|\n"
s+='''
分轮数据与漂移、原始slot均值/p99均在各statistics.json中保留，不用pooled改善掩盖反例。刀1-scoped同宿主批另在knife1-final-statistics.json；最终采纳判断以knife1-product-statistics.json真实before/after为准。

## 复现、身份与卫生

prepare1/scope1、prepare2/shortpack2、prepare3/mask3生成隔离候选；inspect-isa.py直接扫描有ELF symbol边界的gfx1201反汇编；module-inspect记录函数代码/资源身份；layout-proof*.py做地址穷尽。每个*-compile/gold/perf.json锁定源/资产/模块/宿主SHA，run-jobs.ps1执行串行护栏，analyze.py复算统计。actual-module-paths.json额外核对28个候选Gold用的实际模块目录，避免只看requested路径。

原CSV/日志/回执留各忽略Git的case/evidence目录，Git存报告、统计、身份索引、关键源diff和脚本，不存完整.s或二进制大文件。资源未过的CPU原型、命名/路径准备错误与中间正负结果均明确标注；没有把静默回退当新核生效。阶段1/2局部临时大文件按刀清理，最后清理剩余编译副本/缓存与输出，见final-cleanup.json。

没有赢家，因此不生成新生产80模块或改SHA；既有双架构80模块已逐SHA复核未变。没有部署、改玩家配置、替换发行ZIP或push。
'''
(P/'README.md').write_text(s)
