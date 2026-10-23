# C512 helper probe（实际函数提取，root已运行）

本代理只做本地准备/构建与CPU回读；正式COMGR和GPU由root持锁执行。工作根`/home/lmxxf/work/dlss5-gap-phase5-20261023/knife1/`。

- `probe.generated.hip`：直接提取当前生产旧helper与隔离候选实际新helper，旧/新norm只改名；global contract(off)/reassociate(off)与生产生成源一致。源码SHA251aff02…825db1。
- `helper-probe.exe PROBE_HSACO OUTPUT_DIRECTORY [HIP_DEVICE=0]`，SHA214c4500…8a15a3。
- `prepare.py`、`build-host.sh`与build/source receipt可复现。设备源经本地LLVM23前端语法核，IR仍有旧ds_bpermute与新permlanex16，不把本地前端当正式COMGR。

两次kernel launch：

1. norm_lane_probe：32768lane/1024完整wave，每lane两份f8随机有限值，保持原四段8项顺序。输出old/new norm原始32bit，以及lower/upper广播两实现值；独立CPU用tag数组核每个lane应取的另一半row，防止两实现共用错误地图。
2. scalar_pair_probe：65536lane、每lane8值，QKV/prob/AV三路径各524288个输入。每lane第一值覆盖全部65536 half位码，另含FP32随机bit、±0/次正规/±448边界/中点/Inf/多类NaN及显式乘法下溢。保QKV独立mul＋0合同；概率/AV不加额外＋0。

无模型资产。两输出有前后256B canary；所有kernel completion先drain，异常不释放不确定在用资源。norm另检查非负/有限，pack核±0已知参考，避免未写的相同哨兵造成假pass。输入保持由GPU host读回验证。

root实际结果在工作根`helper-probe/`：PASS。`verify.py DIRECTORY --output JSON`随后直接读原始norm/pack/tags三个文件，重新逐值/逐byte比较和canary/±0核验，已输出`cpu-verified.json`：所有diff为0。该回读不再次运行GPU。

限制：norm是full EXEC的随机有限域，pack覆盖全half＋采样FP32/边界，**不是重新穷举全部2^32 FP32**。本probe实例化当前NOF1三路径，不宣NOF0或任意masked EXEC硬件已验。它证明本轮语义与lane选择，不证明性能；完整Frame、pure及拆分性能另看review报告。
