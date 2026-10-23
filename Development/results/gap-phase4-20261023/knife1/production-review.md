# 第四阶段刀1生产默认独立审计

2026-10-23，**PASS**。只本地CPU/源码检查，没有GPU、目标机工具或生产写入；未修改Small资格，未开始刀2。

## 与已测实现的身份

独立实算生产三文件SHA，与`production-identity.json`一致。生产`hip/deep_fast.hip`、`hip/vit_stream.inc`逐字节等于已测最终head-major隔离源；两份生成HIP也实算匹配：

| 模块生成源 | SHA256 |
|---|---|
| deep_fast-packed-fast | `6790d6975340dd40ceabf44dfc62e898aaa1e46f46e4ce27fa391433172f0bd3` |
| vit-stream-fast | `1d9bc17635047b3d2fb0c617d8726c97dee156658e9bd70ec2279745f1cba116` |

对应已测正式HSACO为`68021d286fbfb09d24b38259f9b0ac8c177dec50f837f062047a67d0ce45d12c` / `2cfd9c5f4fb050a9bbbcdd861c2518454444f550701d08f57550c6198e76d320`。本轮不是又换一套kernel。

生产header相对已测候选header的diff只有两行说明和`InitVitVFragment`的开关解析/默认值；**资格函数、local paired路由、布局、API参数和构造调用位置都相同**。header SHA绑定在`production-review-check.json`。

## 默认与优先级

实际规则为：**非空NR_VIT_VFRAG > 非空DLSS5_HIP_VIT_VFRAG > 内置1**。选中值只有字符串`1`启用；`0`及其它非空值均禁用。空值按下一层/default处理。

| NR诊断 | DLSS5变量 | 请求结果 |
|---|---|---|
| 未设/空 | 未设/空 | 1 |
| 未设/空 | 0 | 0 |
| 0 | 1 | 0 |
| 1 | 0 | 1 |
| 非空非法值 | 1 | 0 |

因此正常用户可用`DLSS5_HIP_VIT_VFRAG=0`禁用，实验NR0仍可压住默认1。开关在Network构造时读取；重建/重启后生效，不宣称它具有F9即时热重载语义。

## 成对预热、fallback与热切

- 请求启用即在构造检查producer、400/640两个consumer三导出；缺任一导出available=false，两端全旧，没有“新V格式交旧attention”。旧模块不含新导出时安全退旧。
- 初始Small/MP2也会在构造预查三导出，预热不被运行资格挡住；稍后切Original/MP1不需external wait之后首次查新函数。新布局只在每次Vit的local norm张量上存在，没有跨模式持久V缓存。
- 实际执行资格仍Original、FAST1、MP1、显式AE0、非adaptive_active、指定1600×960/1920×1088/1152、n400/640以及原byte-contract/bin_w5f8路径。Small、FAST0、MP2/3、AE未设或AE1、graph、其它尺寸、skip/observer/dump及不兼容layout保持旧路。
- `vfrag_ae_off`是构造时显式AE0快照；AE1起始即使F8暂时禁复用，也不会被误认成这轮已验AE0。若实例内adaptive_active=true，local eligibility仍立刻拒绝新格式。
- 每次Vit在producer之前只计算一次local vfrag，producer和consumer共用；旧KS2可用时拒绝新格式。MP1→2→1与Small→Original的scope变化不触及Q/K/V权重缓存身份，不额外扩大支持。
- reuse_gate早退保留；这不等于新增AE快路径已验。本次没有删`small_active`或修改`SmallCompatible`。

## 独立CPU检查

`production-review-check.cpp`直接提取当前生产`InitVitVFragment`/`VitVFragmentEligible`原函数文本，HasFn用CPU符号可用性stub；20组实际helper用例通过：默认/空值/禁用/NR优先/非法值、逐个缺新export、初始Small与MP2的构造预热及状态回切、AE快照与active拒绝、三shape及不支持shape、FAST0/graph/skip/layout拒绝。它验证host准入，不冒GPU loader/实机热切测试。

测试fixture首编译因nullptr与字符串initializer-list类型推导失败，显式const char*转换后通过；仅修fixture，未动生产函数。结果与身份见`production-review-check.json`。

核对结果：默认化没有更换已测数学/机器码来源，没有扩大Small或AE资格，没有给缺导出场景留下半切格式。最终host编译与默认构建Gold由主任务回执补齐；此源审不重复GPU性能轮。
