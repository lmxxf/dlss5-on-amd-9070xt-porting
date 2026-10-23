# 刀3独立源审：C512 FFN projection N-pair

2026-10-23。只读CPU审计，不GPU/RTC，不改生产。本报告只审`split_projection_frag_w16`/`_bskip`的隔离N-pair候选；刀2已STOP，不得带入刀3。

## 现有基线与审核重点

旧核位于`hip/deep_fast.hip`的`split_projection_frag_w16_body`。一wave负责16token×64输出通道，每K32步读A0/A1，随后同一A用于4个输出N16片。因此**A复用4个输出片已经存在**，不能把N-pair称为本次新增A复用。

旧K-pair每K32、每N16读16B，4个输出片×16B×16步=1024B/每lane权重。N-pair若每K16为两对N各读16B，则2×16B×32步仍为1024B/每lane。真正可改变的是片段活跃区间、取数时序和调度；不承诺理论流量直接下降。

`CompactC512Body`以`n=(valid+15)&~15`提供完整16token tile；1088的2040点补2048，900的1500点补1504，1152的2160本来16对齐。新核若保持`first=bid/8*16,row=bid%8*64`，则需要`groups=n/2`、threads32。宿主传`count=n*512`时，Run exactname必须设置`groups=count/1024`；漏加名字会落默认`ceil(count/256)`，多派4倍。

## 最终源码审查：PASS，可进GPU数值门

实际候选目录：`/home/lmxxf/work/dlss5-gap-phase2-20261023/knife3/source`；三文件补丁`candidate.patch`。本审计未发现阻断；不代表完整网络性能采纳。身份与CPU检查在`source-review-check.json`，可复跑代码为`source-review-check.cpp`。

### 1. lane/gr/rc与N-pair反解

新host排列为 `[N/32][K/16][gr][N%16][N16-in-N32][8K]`，在512×512矩阵上地址：

`((n/32)*32+k/16)*512 + ((gr*16+rc)*2+(n%32)/16)*8 + k%8`。

新kernel对一个输出64列区间、每个K16步`kt`、每个N32对`np`读取：

`((n0/32)*32+kt)*512 + (gr*16+rc)*16`，`n0=row+np*32+rc`。

其低8B用于`acc[2*np]`，高8B用于`acc[2*np+1]`。独立反解得到原row-major矩阵坐标：

- 输出行 `row+(2*np+j)*16+rc`，j∈{0,1}。
- K列 `kt*16+gr*8+e`，e∈[0,7]。

这正是旧两个N16 fragment在同K16步所需的B操作数；两个half-wave（gr0/1）各取K列的低/高8。`rc`一直表示N16内通道索引，没有误把token行混入权重索引。

CPU独立checker直接包含实际候选`packed_weights.h`调用`FragmentPackedMatrixNPair`，用坐标的低/中/高三个字节分别作唯一身份，避免单一8bit花纹碰撞。对全部262144矩阵字节逐一按上面的消费坐标读回匹配；地址visit count全为1；整个matrix区之外逐字节不变。不是复制host公式再和自己比较。

### 2. FP8矩阵与F32尾范围

`PackedSplitProjectionNPair`先保原`WeightElements(name)==262656`检查，再调用既有`Fp8`量化/精确权重检查，随后仅排列首262144字节。`start=0,rows=columns=512`。

- 新kernel最大16B weight read终点为262144，所有读址16B对齐，均在FP8矩阵内。
- 原scale尾仍用`w[262144+c]`（float索引）：字节1048576..1050623；**这里的262144不是矩阵字节尾直接接scale**，中间保留原布局空区。候选没有把float索引错当byte索引。
- vector保持262656 float即1050624B；新packer不resize、不移动scale；实际checker核全部非矩阵区不变，包括dead gap与512个F32 scale。
- `UploadWeight`沿用Fp8记录的dead区映射；N-pair没有在dead gap写数据，原dense/sparse地址合同未改。此次性能范围仍以实际默认dense产物为准，不将静态说明冒sparse实机验收。

### 3. 每输出累加顺序与输出格式

旧W16的循环顺序是gs0..15，对每个j依次做K16 2gs、2gs+1。新N-pair为kt0..31，对两个np内的j0/j1分别更新；**每一个acc[j]仍严格看到K16 0,1,...31**。不同acc间的指令交错改变，但它们没有共享中间和，不能影响数学。

两种路径均在循环前用同一`H(skip*scale)`初始化acc，`ByteSkip`保持原FP8解码/同一row-major skip索引，最终仍同一`VIT_FAST_H→fmed3→FP8`流程。F32输出与tiled E4M3输出的布局、LDS尾向量化与fence全部复用原模板；新exports均`W32=true,Transposed=false`，没有顺手切换先前落选的nof32/转置路径。

CPU checker还遍历pad n=1504/2048/2160的完整dispatch grid：每个F32输出地址恰好写一次，最末token/通道不越界。这不代替GPU累加位比较；最终Gold仍必须包含新callee实际执行。

### 4. dispatch与ABI

两新导出：`split_projection_frag_npair`和`split_projection_frag_npair_bskip`，参数仍5指针+tokens（无新增参数）。`WAVE`维持wave32。

`Run()`的fast-deep mapping已将两新名字加入精确名字列表，`module=deep_fast,threads=32,groups=count/1024`。调用传`count=n*512`，所以实际n/2组；这与每组16×64工作量完全一致。没有使用松散prefix判定，不会误匹配其它新核。

### 5. 缓存与fallback

新weight key为`name+@proj-npair-v1`，与旧`@proj-frag`分离；同名权重可以在两种布局间并存，不会将旧W16 bytes误交新N-pair消费者。新路同时选择新函数名和新packer，不经后续模糊重映射。

当前实验路由要求：Original（`!small_active`）、FAST1、非fused-FFN-proj、非nof32/FB8、旧W16 projection资格成立、`NR_C512_NPAIR=1`且**两个新exports都存在**。

- 缺任一新export：npair=false，完整回旧weight key+旧callee，不允许只切其中一半。
- NR未设/非1：旧路；静态首次读取在process内固定，不是热配置开关。
- Small/FAST0/FB8：旧路。
- 一次候选process不应为缺export后又临时加载新的同名模块改变句柄；HasFn缓存/模块生命周期仍原机制。
- 本候选没有额外select_module拒绝；这是显式实验开关下的既有module选路。生产采纳前若要支持自定义selector，必须保持“新symbol确实遵此ABI/布局”的边界，不把存在同名symbol当任意第三方模块的数学保证。

FP8饱和、算术宏和其它FFN/QKV/attention模块不变；刀2STOP产物不允许混进基线。trace应证明16个C512块实际命中，而不是只看累计`c512_w16_calls`（该计数在新分支前就可能增加）；本次新计数`C512_NPAIR calls`及actual函数名更直接。

## GPU门的最小核对

1. Original三尺寸完整Gold对刀1采纳后基线，finite/repeat；新两个callee的实际命中和次数。
2. bskip与F32-skip都覆盖。默认byte链可能16块都走bskip，普通skip需一个受控关闭相应byte边的短门。
3. 合法模块缺一个新export时，module load成功、npair calls0、旧weight/callee成对、输出Gold。删坏整个模块不是fallback测试。
4. 最终热点ISA检查VGPR/SGPR/LDS/spill、old函数身份以及K16链，不能只报静态weight load条数变少。
5. 三档ABBA按原门，若只有资源/静态指令改善而无稳定整网收益则STOP，不扩扫变体。

CPU结果：`PASS actual N-pair pack helper: 262144 unique matrix bytes x 3 coordinate-byte passes; all nonmatrix bytes unchanged; padded 1504/2048/2160 grids cover each output exactly once`。

