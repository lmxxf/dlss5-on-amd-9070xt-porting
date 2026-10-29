# 执行状态（工作快照）

Step1 已完成并独立提交 4a260061。统计 step1-statistics.json：Original HIP8.301702917/ACO5.670746875ms（+46.395%）；Small1080 5.005096073/3.430510417（+45.899%）；Small900 3.841217344/2.607886458（+47.292%）。新版原样支持驱动，53个SPV匹配NRP；源原树、锁版树未变，Step1 raw大文件已清。Step1 payload保留供后续。

Step2 初始 dense Record-events：漏SP6调用（147非153）且Small负event，保留step2/evidence。修复SP并sparse仍负，step2-sparse/evidence；single-pair no outer event仍在C256得到−0.02898ms，step2-single-probe（尚待collect）。不是候选性能回退，属于计时仪器失败，不用作热点裁定。

当前 prepare-profile.py 生成隔离 Network+SP helper，API native launch start/stop timestamps（hipExtModuleLaunchKernel），每帧仅采一个dispatch，PHASE4_DISPATCHES明确153/83；原生产 opt.profile=false/observer关闭/PDL路径不改。对实际Handle从functions缓存反查物理symbol（含C32 _w16和compact选中核）。无嵌套whole-network GPU events，profile timing.csv gpu_ms=-1是明确不可用标志，CPU wall可用于探针扰动对照；kernels.csv才是单核GPU时间。

当前远端运行 step2-native-probe.json（Original306 measured、Small166，80warm）。本轮 exec session 20013。如成功，正式profile每档三轮，Original918测量/轮（6×153），Small498/轮（6×83），每dispatch18个观测；控制用160测量。M用--per-layer --dispatch-grid、160测量，三轮。先改analyze-step2.py适配单采样每帧、按每dispatch均值再求族总和。必须验证153/83完整覆盖、Gold与Step1相同，明确launch event不含release/cache flush（AMD API官方文档）。如果native probe失败必须读具体原因，不循环同一种Record方案。

本地work/c32-baseline和work/c64-baseline已正式LLVM23 row-opts / -real-true16重编；SHA分别8a5d9dd.../475f515...与生产完全同，含可重组装.hsaco.s。后续热点以最终有效诊断排名为准。初始不完整探针暂见C128 bi_bo族~1.11ms最大，不能作最终结论。

Step3尚未开始。3A需保持B与数学顺序，适配实际头号热点深预取；注意C32/C64源HIP_KERNEL_WPE=0原本无显式waves下限，不得假称解锁已有硬限制。资源0spill才上GPU。3B只改一个目标函数，在完整编译器.S中做严格局部load提前，VMEM顺序/等待计数/EXEC/回边必须证明。C32 contract前一条Expand offset2560 load可提前跨pack；注意s_delay_alu SKIP跨指令需维持原target（选移动到相关delay之前且此前delay第二target已消耗可避免改delay）。C128需另枚举可证明窗口。必须原始函数/模块资源与ASMdiff验证、七档Gold42帧、单核和四档ABBA；负则STOP，赢家才同步80+SHA。

只在297仓操作；隔离M构建唯一例外路径 /home/lmxxf/work/dlss5-phase4-mochi-build。原M源码 /home/lmxxf/work/dlss5-mochi-008只读，旧d1185树只读，不动游戏/ZIP。远端R4根、run-jobs.ps1统一guard；startup_compile180秒但game watchdog15秒，strict serial。无push/amend/Coauthor。Step2/Step3完成各清理并独立commit，全部完成才final。

更新：native-launch probe成功（两个模型、所有153/83 dispatch轮转覆盖，Gold同），已启动正式 step2-native.json，exec session63778。三轮N profile每轮918/498帧、每dispatch6观测，共18；N-control与M-control/profile均160。全网GPU在N-profile明确不可用（timing.csv -1，不解释成GPU负时间），单核GPU来自ext launch start/stop事件、无hipEventRecord外围计时，CPUwall量化扰动。analyze-step2.py已适配此采样/重构方法。初始三个Record事件版本全部保留无效，不能作热点排名。
