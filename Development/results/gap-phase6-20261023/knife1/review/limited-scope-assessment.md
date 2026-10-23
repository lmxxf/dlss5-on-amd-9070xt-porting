# 第六阶段限定scope独立评估

仅本地CPU复算，未新增实验、未删样本。84个Frame槽与12个pure槽逐轮/合并均值和p99吻合；详细值在independent-frame-review.json、independent-pure-review.json。

**按本项目已使用的pooled mean＋pooled p99工程门，限定Original900、Small900、Small1080可以成立；证据只支持小幅限域收益，不支持全scope采纳或“所有尾都改善”。** 如果改成每轮p99都不退的门，这三个scope也都不通过，不能临时换口径。

|profile|Frame均值差µs|pooled p99差µs|独立观察|
|---|---:|---:|---|
|Original900/1600×960|−11.885|−15.124|3轮mean、6相邻对均胜；R1/R3 p99仍+8.033/+7.038|
|Small900/1600×1024|−7.692|−8.495|最弱入选项；R2 mean+1.656、p99+13.155，须明示|
|Small1080/1920×1152|−11.083|−7.045|3轮mean胜、5/6相邻对胜；R3 p99+17.340|
|Original1088|−2.035|−22.968|mean不稳定，R1+19.990；保持V-only|
|Original1152|−18.882|**+18.668**|R3 p99+123.106；不能用mean盖尾退|
|Small720|+3.704|+3.212|不采纳|
|Smallfree1088|−1.669|**+41.400**|两轮mean退，保持V-only|

相邻对只用于展示一致性，不是替代ABBA或新设验收规则。按shape做路线选择是本轮测后决定，应保留全部七档原数据，不能只发表入选三个。Small900尤其不要写成稳健显著收益。

## pure是全scope实验，不是限定生产1088收益

全scope Original1088 pure：8.336080875→8.322504021ms，mean−13.577µs、3轮mean胜，但pooled p99 **+10.364µs**，R2+27.834µs。数值门通过不意味着尾部门通过。限定版在Original1088继续V-only，故这13.577µs不能算“生产1088已追回”，也不能从之前Mochi差距中扣除。

## 限定helper与身份

隔离header SHA6461f1a9…30b8已核。实际helper只接受：Original(1600,960,400)、Small(1600,1024,448)、Small(1920,1152,640)。本地从实际函数体核fixture并执行120组，恰3组合接受。最终local qkfrag还与既有vfrag完整资格、requested、available相与，两端共用同一bool。默认1，NR/公开0关闭；**显式1也不绕过shape限制**。缺任一新导出仍整对退回V-only，VFRAG本身关闭时保持更早plain路径。

原84槽与pure执行的是全scope host；其三exe已归档all-scope-tested-hosts并逐SHA对原host-build-identity。新限定三exe另对limited-host-build-identity，SHA不同，未混作原性能对象。两GPU生成源SHA保持。默认门还应核3shape命中、4shape拒绝、关闭/缺export及AE输出；这里不提前冒称新host这些门已完成。

静态连续512B/每lane16B是请求组织，逻辑QKV字节没有减少。没有cache miss/带宽计数证据，不能解释成大幅DRAM收益或把本轮弱收益包装为硬件瓶颈已解决。
