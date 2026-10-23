# 刀3：C512 projection N-pair，隔离候选

2026-10-23。基线为刀1最终生产header和原40模块；刀2已STOP，不混入MIX。仅一个N-pair候选，没有扫参。GPU/正式COMGR由root统一持锁执行，本代理只做CPU实现、索引证明和编译筛查。

## 改了什么

旧 `split_projection_frag_w16[_bskip]` 非Transposed路径已在每个K32内读取A0/A1，复用于4个N16输出片。本次**没有新增A复用，也没有把权重或A总读取量减半**。

新路径把B权重的16B配对轴从K改为N：

- 旧W16：同一个N16片的连续两个K16 fragment放同一16B；一轮K32读取4份B16B。
- 新N-pair：同一个K16的相邻两个N16 fragment放同一16B；一轮K16读取2份B16B，循环迭代数翻倍。
- 每个K16先取一个A，依次更新相邻两N片、再更新另两片。每个输出累加器仍经历K16 `0,1,...,31`，原skip-first `H(skip*scale)`、末尾 `VIT_FAST_H`、FP8编码、float/out8写回及LDS尾处理完全沿用。
- 新核用K16 loop `unroll 4`；相较原K32 `unroll 4`，展开体减半、循环次数增加，这是候选组织的一部分。静态WMMA数减少不代表动态矩阵运算少了。

## 布局和索引

矩阵512×512，保持原分配容量和FP32尾地址。FP8矩阵字节布局：

`[N/32][K/16][gr][N%16][N16-in-N32][8 consecutive K]`

对原系数`W[n,k]`，目标字节下标：

```
((n/32)*(K/16)+k/16)*512
+ (((k%16)/8*16+n%16)*2+(n%32)/16)*8+k%8
```

核内`row=block%8*64`，`np=0,1`，`n0=row+np*32+rc`，16B读取起点：

```
((n0/32)*32+kt)*512+(gr*16+rc)*16
```

其低/高8B分别是`W[row+np*32+rc, kt*16+gr*8:...]`与`W[row+np*32+16+rc, kt*16+gr*8:...]`。每次读取16B对齐；scale仍从float索引262144后读取。

实际执行新CPU packing函数的穷尽检查：[index-proof.cpp](index-proof.cpp)、[结果](index-proof.json)：262144矩阵字节逐项吻合、映射双射，全部788480非矩阵字节保持，A地址与原gs/h对应相同。独立审计另覆盖多个坐标模式与1504/2048/2160 token grids，见 `source-review.md`。

## 新接口、缓存、回落

- 新导出：`split_projection_frag_npair`、`split_projection_frag_npair_bskip`；旧导出保留。
- 新权重键：`@proj-npair-v1`，不复用旧`@proj-frag`。
- 实验开关：**`NR_C512_NPAIR=1`**（默认关闭，静态读一次）；`0`旧路。
- 资格：Original、FAST1、已有W16路径、非融合FFN-proj、非Transposed/FB8，且**两个新导出都存在**。不满足则旧核＋旧布局一起回落；Small不进入。
- `Run()`的exactname派发表已加两个新名字，`groups=count/1024`，wave32；仍用原pad16的`n`，不拿未补边valid算网格。
- 仅在实际调用新核时累加计数，析构打印`C512_NPAIR requested=... calls=...`。每完整71块单遍预期16次；调用计数需由实际日志核对，不等同请求值。

每个模型/路径独立tag防布局混读。单独冷启动采用新路径时替代旧投影权重；若同一Network后来同时走旧路，缓存可能同时保留两份，故不宣所有切换场景零额外显存。

## CPU编译和ISA筛选

本地LLVM21通过，**只作语法/初筛，不能代替正式COMGR**。同一LLVM21下旧导出机器码保持逐字节同，新路径：

|核|VGPR|SGPR|LDS/spill|静态指令|代码bytes|静态WMMA|
|---|---:|---:|---|---:|---:|---:|
|旧w16|99|17|1024/0|613|4212|32|
|新npair|81|16|1024/0|566|3888|16|
|旧w16_bskip|99|17|1024/0|627|4256|32|
|新npair_bskip|81|16|1024/0|580|3932|16|

寄存器与代码体减少足以进入正式编译/Gold门，但不能据此宣布性能收益；循环迭代翻倍，动态成本由GPU实际验证。

### 正式COMGR已核

root持锁编译并取回：baseline整模块SHA `00e39349bc8a1a02e5a61b4606d1541d5ed1e4988de8c17db3cc5c8380c095ce` 与生产相同；candidate SHA `bd1c08ddd28dc96dfde4ab458d5fd2e5badd56e60fe0380358d96b2a07946b1a`。

**旧89个函数在候选91函数模块内全部机器码逐字节相同；旧kernel descriptor资源/浮点配置字段全部相同（入口地址变化单独排除）。只有两个新export。** [逐函数核验](old-exports-identity.json)

|正式COMGR核|VGPR/SGPR|LDS/spill|静态指令|代码bytes|WMMA/VMEM读/wait|
|---|---|---|---:|---:|---|
|旧w16|100/17|1024/0|603|4156|32/60/63|
|新npair|80/16|1024/0|574|3884|16/48/64|
|旧w16_bskip|100/17|1024/0|630|4288|32/60/71|
|新npair_bskip|80/16|1024/0|590|3976|16/48/63|

动态工作量和新循环迭代次数不能忽略，特别new ordinary静态wait反而+1；正式性能只由三轮ABBA决定。为隔离路由，root准备**两臂同一个candidate模块＋同一个新host**，仅 `NR_C512_NPAIR=0/1` 切换；旧导出字节保持已证，baseline不混不同模块映射。

`cpu-isa.json`保完整opcodes和代码hash。`isa-check.py <正式baseline.hsaco> <正式candidate.hsaco>`可生成`comgr-isa.json`，用于正式编译后再次核查。

## 给主进程的文件

工作根 `/home/lmxxf/work/dlss5-gap-phase2-20261023/knife3/`：

- `baseline.generated.hip`：原正式deep_fast-packed-fast配方，文本与10-17最终载荷同。
- `candidate.generated.hip`：同配方＋新模板分支/两个导出。两份也归档在本目录`generated/`。
- `frame.exe`：当前刀1header＋N-pair路由，正式O2；SHA `1b90d1358028031e7af6263f71cba0e025b7c4df86f6f0f35b66f8dc4dadd6ff`。
- `source/`：隔离HIP/src/Development/HIP，生产目录未改。

COMGR以当前`rtc_compile-current.exe`、`comgr gfx1201`、空`RTC_EXTRA_OPTS`分别编两份；baseline先核当前模块SHA/实际kernel代码。候选目录只替换`deep_fast-packed-fast.hsaco`，其它39模块保持刀1。同一新host分别`NR_C512_NPAIR=0/1`，先逐帧Gold和实际命中，再三轮ABBA。缺export回落另测，避免requested=1实测旧路。

复现入口：[prepare.py](prepare.py)、[candidate.patch](candidate.patch)、[build-host.sh](build-host.sh)、[source-identity.json](source-identity.json)、[cpu-build.json](cpu-build.json)。`prepare.py`会覆盖隔离副本，运行实验期间勿重跑。

## GPU结题：正确但目标档无收益（STOP）

正式COMGR重建旧模块whole SHA与现生产一致；candidate旧89函数机器码/资源字段全同，仅新增2函数。性能两臂使用同一新host和同一candidate模块，仅NR_C512_NPAIR=0/1切换，另外39模块保持原SHA。每个性能槽核实requested与实际调用：baseline0次，candidate3856次（240帧加1预热准备帧，每帧16块），不把静默fallback当候选。

- 三档×三图样×两臂×3帧：54帧Gold全同。
- 额外关闭C512块间byte路径的普通F32 skip双臂各3帧：6帧Gold同。
- 1152/900使用实际旧模块缺两新导出，requested1、calls0，旧布局/核回退输出各3帧同：6帧。
- 全部66帧finite/repeat门通过。缺export与普通skip证据在对应gold-*目录，不以默认bskip覆盖冒全部分支。

完整Frame每档3轮4-slot ABBA（240弃80，每arm960稳态样本）：

|档位|旧ms|N-pair ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.461980|6.462889|+0.000909|−0.090785|
|1088|8.869993|8.876145|**+0.006152**|**+0.021655**|
|1152|9.158257|9.146322|−0.011935|−0.079239|

1088三轮均值分别+10.677/+0.793/+6.986µs，目标档没有收益；900方向不稳定，合并近零。1152均值小幅改善，但R2p99+36.063µs，部分control漂移大于均值差，不能据此宣称全面追平或抵掉1088退步。**本轮整体STOP，不合生产、不扫unroll/预取变体、不向QKV扩散。** 保留1152小正值作为本次实际结果，不改写成“所有档都慢”；也不将一个projection实现否定所有N-pair方案。

新核VGPR100→80是实证，但总A/权重读取字节没减，循环展开/迭代和地址调度同时变化；没有硬件stall计数，不把性能归为某个单独因素。原CSV、请求/实际计数、全部原始轮次见evidence/与statistics.json。GPU/RTC均遵game-check/≥100GB/gpu.lock/15秒看门狗，最终释放锁。生产保刀1最终资格与原80模块；未安装游戏/发包。
