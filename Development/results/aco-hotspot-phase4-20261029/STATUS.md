# 执行状态（工作快照）

Step1 已完成并独立提交 4a260061。统计 step1-statistics.json：Original HIP8.301702917/ACO5.670746875ms（+46.395%）；Small1080 5.005096073/3.430510417（+45.899%）；Small900 3.841217344/2.607886458（+47.292%）。新版原样支持驱动，53个SPV匹配NRP；源原树、锁版树未变，Step1 raw大文件已清。Step1 payload保留供后续。

Step2 初始 dense Record-events：漏SP6调用（147非153）且Small负event，保留step2/evidence。修复SP并sparse仍负，step2-sparse/evidence；single-pair no outer event仍在C256得到−0.02898ms，step2-single-probe（尚待collect）。不是候选性能回退，属于计时仪器失败，不用作热点裁定。

当前 prepare-profile.py 生成隔离 Network+SP helper，API native launch start/stop timestamps（hipExtModuleLaunchKernel），每帧仅采一个dispatch，PHASE4_DISPATCHES明确153/83；原生产 opt.profile=false/observer关闭/PDL路径不改。对实际Handle从functions缓存反查物理symbol（含C32 _w16和compact选中核）。无嵌套whole-network GPU events，profile timing.csv gpu_ms=-1是明确不可用标志，CPU wall可用于探针扰动对照；kernels.csv才是单核GPU时间。

当前远端运行 step2-native-probe.json（Original306 measured、Small166，80warm）。本轮 exec session 20013。如成功，正式profile每档三轮，Original918测量/轮（6×153），Small498/轮（6×83），每dispatch18个观测；控制用160测量。M用--per-layer --dispatch-grid、160测量，三轮。先改analyze-step2.py适配单采样每帧、按每dispatch均值再求族总和。必须验证153/83完整覆盖、Gold与Step1相同，明确launch event不含release/cache flush（AMD API官方文档）。如果native probe失败必须读具体原因，不循环同一种Record方案。

本地work/c32-baseline和work/c64-baseline已正式LLVM23 row-opts / -real-true16重编；SHA分别8a5d9dd.../475f515...与生产完全同，含可重组装.hsaco.s。后续热点以最终有效诊断排名为准。初始不完整探针暂见C128 bi_bo族~1.11ms最大，不能作最终结论。

Step3尚未开始。3A需保持B与数学顺序，适配实际头号热点深预取；注意C32/C64源HIP_KERNEL_WPE=0原本无显式waves下限，不得假称解锁已有硬限制。资源0spill才上GPU。3B只改一个目标函数，在完整编译器.S中做严格局部load提前，VMEM顺序/等待计数/EXEC/回边必须证明。C32 contract前一条Expand offset2560 load可提前跨pack；注意s_delay_alu SKIP跨指令需维持原target（选移动到相关delay之前且此前delay第二target已消耗可避免改delay）。C128需另枚举可证明窗口。必须原始函数/模块资源与ASMdiff验证、七档Gold42帧、单核和四档ABBA；负则STOP，赢家才同步80+SHA。

只在297仓操作；隔离M构建唯一例外路径 /home/lmxxf/work/dlss5-phase4-mochi-build。原M源码 /home/lmxxf/work/dlss5-mochi-008只读，旧d1185树只读，不动游戏/ZIP。远端R4根、run-jobs.ps1统一guard；startup_compile180秒但game watchdog15秒，strict serial。无push/amend/Coauthor。Step2/Step3完成各清理并独立commit，全部完成才final。

更新：native-launch probe成功（两个模型、所有153/83 dispatch轮转覆盖，Gold同），已启动正式 step2-native.json，exec session63778。三轮N profile每轮918/498帧、每dispatch6观测，共18；N-control与M-control/profile均160。全网GPU在N-profile明确不可用（timing.csv -1，不解释成GPU负时间），单核GPU来自ext launch start/stop事件、无hipEventRecord外围计时，CPUwall量化扰动。analyze-step2.py已适配此采样/重构方法。初始三个Record事件版本全部保留无效，不能作热点排名。

更新：Step2真正有效方法为prepare-profile-device.py的独立GPU实时钟小核（stamp.hip，s_sendmsg MSG_RTN_GET_REALTIME），运行时属性10017确认100000kHz。先前所有HIP短事件方案都因负值/总和不闭合而拒用，不采native-fenced排名。有效step2-device 12N上下文 +step2-native中的12M上下文，完整153/83与123/71，三轮采样18/dispatch，校正0.8µs邻接stamp开销，总和闭合0.7–1.0%。Step2已提交a8103346。选择C128 bi_bo为主靶（0.819ms，SP0.809近并列）。

Step3 CPU：3A源码prefetch4个i2 Contract权重至Expand前，B4/T2/数学顺序保留，仅c128_wave2_bi_bo改变；VGPR242→248、SGPR26/LDS16384/private0/spill0，实际.kd资源编码全同（没有驻留下降）。全7档Gold42帧已过。
3B prepare3b.py：完整CFG may-pending-write含回边，load v213:214从第293行提前至290行，跨v_add3/v_or3两独立地址VALU；clause14→13，memory issue顺序、wait计数和所有s_delay目标保持，旧位置下一VMEM保留隐式VALU完成屏障（LLVM23 InsertDelayAlu/FLAT语义）。未增/改寄存器。完整重新链接有46个非目标函数字节变化，未上机；目标函数roundtrip与生产逐字节同后，同尺寸注入原始ELF，全部136其他函数和metadata bytes不变。3b-resource-comparison.json/3b-safety-proof.json。全7档Gold42帧已过。

当前终端step3-sequence.sh session86949：3B Gold已完成，正串行跑3A-kernel（Original306/Small166测量，3轮ABBA）、3A四档48slot性能，再3B同样kernel/perf。始终同一baseline模块，不叠加。脚本每role跑完自动collect并用analyze.py生成wall统计；还需手动python3 analyze3-kernel.py 3a/3b。不要重复启动。有赢才合并双架构80/SHA并重新评估，负则STOP/failed-experiments/保留归因/清理并Step3独立commit。最终README+WorkingPlan/DevHistory、锁版树/原M源码hash复核，清理全部临时大文件（保留必要payload），不得push/amend/Coauthor，不动游戏/ZIP。

最终状态：Step1 4a260061、Step2 a8103346完成；Step3两候选全部闭环并均STOP。3A Original单核−0.521µs/次但整网1088+2.481µs/p99+15.682；3B单核不稳，整网900三轮均值全退+4.544µs/p99+15.587。每候选Gold42帧、4档48性能slot、2档24诊断slot均完成；共84/96/48。生产源码和80模块/SHA相对41c28059完全未变，原M源和d1185树未变。全部临时编译/反汇编/raw输出/失败二进制/隔离M构建树已清，payload保留复现输入/宿主/shader；锁和进程已确认释放。README/step3-README/WorkingPlan/DevHistory/failed-experiments已更新；等待最后git格式检查与Stage3独立commit。无待跑GPU/RTC作业。
