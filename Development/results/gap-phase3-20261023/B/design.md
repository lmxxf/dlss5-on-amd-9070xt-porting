# B协议设计：仅C512 FFN→Projection这一条边

2026-10-23，CPU设计审查稿。尚未GPU；root已确定直接完整Frame短Gold/失败/trace安全门，安全通过后完整三档ABBA，不以短pair性能替代用户范围。生产基线仍phase2刀1；A和phase2刀2/3全部不带入。

## 单条边与数学

生产者仍`c512_ffn_one_w2f8_w16_body<false,true>`：64线程/2wave，每wave输出16token×64通道组，8个group完成一个16token×512片。消费者仍`split_projection_frag_w16_body<true,false,true,false>`：32线程/wave，每WG输出16token×64列，读该tile完整512通道。新wrapper只做发布/等待，不改任何算术、数据布局或权重。

生产者groups=`ceil(n/32)*8`；消费者groups=`n/2`；ready容量`n/16`个uint64。900n1504、1088n2048、1152n2160。1152最后producer WG的第二wave无效，必须共同过WG屏障但不发布`ready[135]`。

## 发布与获取

1. 每64线程producer先完整执行旧body（旧body无早return，只有first<tokens内部条件）。
2. 全64线程WG barrier → 每lane agent release fence → WG barrier。
3. 每wave lane0按`first<tokens`给`ready[first/16]`执行uint64 `fetch_add(1, ACQ_REL, AGENT)`。一tile8次贡献；ACQ_REL RMW链汇合8个发布者。
4. consumer lane0通过agent acquire load等待`ready[tile] >= target`；所有lane在同一wave内广播结果。成功后每lane执行agent acquire fence，再进入原body。没有输入读发生在此之前。
5. 有界等待：固定最大16384次poll，每次未ready后`s_sleep(1)`；不声称此值等于固定毫秒。超限经wave广播后全wave先把自有16token×64列F32/out8写零，leader给sticky uint32 status做atomic OR，再统一返回；**不读contract/skip/weight**。这只是定义失败实验的下游输入，最终整批必须拒绝，不能称正确fallback。

所有额外barrier/fence/原子/等待属于候选成本，不从计时里拿掉。源审与ISA需验证release先于信号、acquire/fence先于数据读。

## epoch与回绕

每次候选pair的host target增加8，GPU counters只在构造初始化一次。每次所有tile都收到8贡献，pair间按原ordered stream完结；不逐帧清零，不在外部producer wait之后做同步reset。target超过uint64最大值−8时永久关闭此实例链化，走原ordered核；不重置、不回绕。实验harness限制明确pair次数并检查加法上界。

## 最终完整Frame失败与owner方案（取代初稿pair限定）

root决定用完整Frame短Gold＋受控超时零写probe＋ordered/AnyOrder trace，再完整三档ABBA。consumer失败按前述零写+sticky，后继普通ordered QKV等核只读已定义数据。最终计时外`CheckChainStatus`先选owner device、显式stream drain，再读sticky与16×tiles ready，任何status/counter mismatch拒整批。

构造初始化counter/status并预热两个新函数，初始counter值可指定8倍数供carry/极限测试。每16个C512 block各有独立counter槽和target，每次实际producer入队前target+8。dup/profile/graph/非固定三档/非OriginalFAST1/非MP1等退出实验资格；producer不继承pdl_anyorder，consumer仅局部AnyOrder。

候选持有16槽compact+contract8至当前Frame原有Flush完成。Release在elapsed截止前执行并计入candidate；NR0不retain、不调用DiagnosticNetwork/Release。cold prewarm每次Enqueue+Synchronize后也Release，防首实际帧被warmup引用撑大pool。候选若在external wait之后出现pool miss直接拒绝，不同步malloc。失败/析构先drain后清owner。

本实验不是游戏/产品fallback；根目录生产源和玩家载荷不改。

## AnyOrder与重叠证据

Producer普通ordered launch；Consumer使用既有HIP7 `hipExtModuleLaunchKernel(... flags=1)`单条边AnyOrder。每条边随后原有ordered QKV保证该边结束；每帧既有Flush后才释放retain并开始下一帧。绝不跨实例共用counter。

正确性先比较原ordered pair、带协议但ordered pair、带协议AnyOrder pair；counter全等target、status0、contract/output两种格式逐字节同。必须包括2160尾wave及第二个epoch无reset。

重叠诊断另编trace版本：producer记录完成全部数据stores后的pre-publication时间；consumer记录完成第一条真实WMMA并把结果store/fence之后的时间。比较消费者真实计算完成是否早于最后producer的pre-publication；ordered控制应无这种重叠。仅consumer进入或等待开始不算有效计算重叠。trace关版本才能量性能。

## 与旧vit-tile-ready区别及继承负账

10-12旧ViT contract→QKV已经证明真实WMMA重叠，但pair +25.727µs/+36.7% STOP。本次换成C512 FFN→projection、8组/tile、两wave生产者，并用64bit累计epoch去掉每pair reset。该变化提供重试条件，不证明新协议必然更便宜；barrier/release/acquire/调度成本仍可能压过重叠收益。旧负结果照留。
