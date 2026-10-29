from pathlib import Path
import json,csv,hashlib,subprocess
p=Path(__file__).parent;m=json.loads((p/'step2-hotspot-breakdown.json').read_text());m['selection']={'primary':{'module':'c64-wave2-fast.hsaco','kernel':'c128_wave2_bi_bo','logical_module':'c64_wave2','reason':'Original aggregate largest, near-tied with SP; isolated FFN branch and canonical assembly permit bounded intervention. Not a claim that SP is statistically smaller.'},'top3_original':['c128_wave2_bi_bo','sp_run256_w16','c32_wave1_post_b8_w16'],'small_priority':['c32_wave1_post_b8_w16','c32_wave1_prefix_b8d_w16'],'hardware_bandwidth_utilization':None,'hardware_wmma_utilization':None,'utilization_note':'No fresh SPM/SQTT throughput counters; timestamps/resource metadata cannot establish peak utilization.'};(p/'step2-hotspot-breakdown.json').write_text(json.dumps(m,indent=2))
text='''# Step 2：逐核 GPU 计时、仪器校准与热点选择

## 有效测量方法

短区间HIP事件在当前Windows运行时没有通过校准，未据其排名。先后观察到负值、默认native-event重构20ms远大于8ms整网、no-system-fence native-event重构数百ms；长批次HIP event在CPU/raw-clock区间随8/32/64/128次网络执行增长至1s时仍报告约75ms。保留全部失败探针，不能把它们解释成真实kernel成本或编译器病灶。默认fence可能贡献探针开销，但未隔离证明其是唯一原因。

最终方法：独立小核通过gfx1201 `s_sendmsg_rtn_b64 MSG_RTN_GET_REALTIME`（消息0x83）读同一GPU实时钟。每帧只夹住一个业务dispatch，轮转覆盖全部153/83次；另两个小核夹住全网，每诊断帧增加4个独立时钟dispatch。原计算kernel代码、实际Fn物理符号、PDL/持久核路由及默认生产快路径不改。SP专用launch也完整纳入。

单位直接来自 `hipDeviceGetAttribute(hipDeviceAttributeWallClockRate=10017)`：100000kHz，即100000ticks/ms。该枚举按ROCm7.2.0官方头文件核对；不是shader频率。长窗口CPU墙钟交叉校验约99381/99500ticks/ms（含窗口外CPU/同步），误差<1%；不使用饱和的HIP长批事件拟合单位。相邻stamp对的100次中位数为80ticks（0.8µs），逐样本原始区间保留，校正时不截掉负值。总和闭合约1%，仍不把诊断模型当未插桩原图的精确critical path。

Original三轮每轮80warm+918测量，Small每轮80warm+498测量；每个业务dispatch每轮6次、共18次直接计时。全网GPU Event和CPU wall对照单独保留，不能用这两者的差强行修正kernel。Gold全部匹配Step1对应输出、invalid=0。对手使用原有 --per-layer --dispatch-grid，三轮每轮80warm+160测量，完整123/71个dispatch；没有修改对手shader或用INT4/EXPERIMENTAL。

API参考：[设备属性与单位](https://raw.githubusercontent.com/ROCm/hip/rocm-7.2.0/include/hip/hip_runtime_api.h)，[launch事件范围](https://rocm.docs.amd.com/projects/HIP/en/docs-7.2.3/reference/hip_runtime_api/modules/execution_control.html)，[事件fence语义](https://rocm.docs.amd.com/projects/HIP/en/docs-7.2.0/doxygen/html/group___global_defs.html)。这些文档不能代替本机校准，因此最终不使用失败的HIP短事件结果。

## 业务核账本

以下是同名物理kernel各次调用的GPU实时钟均值之和，单位ms/帧。原始逐dispatch、各轮、对手每pass与计时扰动均见 step2-hotspot-breakdown.json 和忽略Git的evidence。
'''
for name in ['original1088','small1080']:
 v=m[name];text+=f'\n### {name}\n\n| 物理核 | GPU ms/帧 | 占重构核总和 |\n|---|---:|---:|\n'
 for x in v['hip'][:8]:text+=f"| {x['kernel']} | {x['mean_per_frame_ms']:.6f} | {x['share_percent']:.2f}% |\n"
 text+='\n| 轮 | 重构核总和 | 全网实时钟区间 | 比值 | 无探针CPU wall | 诊断CPU wall |\n|---|---:|---:|---:|---:|---:|\n'
 for x in v['hip_probe_effect']:text+=f"| {x['round']} | {x['reconstructed_dispatch_sum_ms']:.6f} | {x['profile_network_realtime_ms']:.6f} | {x['reconstruction_ratio']:.5f} | {x['control_cpu_ms']:.6f} | {x['profile_cpu_ms']:.6f} |\n"
 text+='\n对手较大stage（融合边界不同，不能逐行相减）：\n\n| Stage/kernel | GPU ms/帧 |\n|---|---:|\n'
 for x in v['aco'][:6]:text+=f"| {x['stage_kernel']} | {x['mean_per_frame_ms']:.6f} |\n"
text+='''
## Top 3 与后续干预

1. **C128 wave2 bi_bo**：Original约0.819ms/帧、10次调用。242VGPR、26SGPR、16KiB LDS、0spill。FFN Expand/量化/Contract处存在可审计的权重读取与寄存器保活问题，保持B4复用测试深预取及最小ISA调整。它与SP接近并列，不夸大排名显著性；选它是因为明确的隔离干预条件。
2. **sp_run256_w16**：Original约0.809ms/帧、两个多层持久执行阶段。绝对大头，但包含内部调度和多层工作，不能当普通单层Swin来对比。此前降低VGPR已失败，仍需分别验证预取/调度，未承诺有可收收益。
3. **C32 post**：Original约0.728ms、Small约0.762ms；对手post-family约0.531/0.556ms，融合与输出接口不同，只支持继续定位候选区，不能把差全部归因某条ISA。Small的prefix约0.737ms也同样优先。prefix还存在我方每帧内联噪声计算、对手预建noise field等边界差异，不是纯编译器同数学路径对比。

C128生产配置 HIP_KERNEL_WPE=0，原本没有显式waves_per_eu下限可放开。不能假称移除现有硬限制；Step3A在保持该无下限配置/B4/数学顺序下测试更深预取，观察真实VGPR、spill与性能。资源不合格即停止，不以“汇编少/寄存器低”替代收益。

本轮只有GPU时间与静态资源，没有fresh SQTT/SPM带宽/WMMA利用率计数器；不编造有效带宽或峰值占比。各实现的融合、舍入、噪声、提交边界有差异，Step1仍是既定计时器口径的同批差距，不是全部纯机器码/数学计算差距。当前证据足以否定已证接近硬件上限，尚不足以给出离理论峰值的百分比。

诊断按相同guard串行执行，所有无效计时与反例保留。Step2 raw输出/每slot临时exe清理后独立提交；仅保留后续需要的设备时钟模块、诊断宿主和canonical基线。生产源码、80模块、游戏、ZIP、锁版M文件树不改。
'''
(p/'step2-README.md').write_text(text)
