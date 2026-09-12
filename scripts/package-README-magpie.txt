DLSS5-AMD 0.14 · Magpie 版
============================

把 DLSS 5 的神经网络（DLSSNR）跑在 AMD RX 9070 XT（RDNA4）上，以 Magpie 窗口缩放器为载体：
任何能开 1920x1080 无边框窗口的游戏都能用，不需要游戏自己支持 FSR 或 DLSS。
Magpie 抓取游戏窗口 -> Magpie 的 FSR3 效果（含 AMD 光流估运动向量）-> 本插件截下 FSR3 的
ffxDispatch 调用，换成 DLSS 5 网络 -> Magpie 显示。

本包内容
--------
  Magpie.exe 及其文件            Magpie 实验分支 0.6.6（SAOG0721/Magpie，GPL-3，许可见 LICENSE-Magpie.txt；A 卡用不到的 NVIDIA 运行库已去掉）
  config\config.json             Magpie 便携模式配置（预设好的效果组和选项）
  dxgi.dll                       ReShade 6.8 加载器（原版，未修改；放在 Magpie.exe 旁边就会被加载）
  dlss5-amd.addon64              本移植的 DLL（.addon64 是 ReShade 的扩展名，不要改名）
  DLSS5-D3D12-721\               微软 DirectX 12 Agility SDK 1.721 预览运行时，Shader Model 6.10 需要它
  DLSS5-AMD\                     权重、编译好的着色器、运行参数（必须和 dlss5-amd.addon64 在同一目录）
  SHA256SUMS.txt                 文件校验

需要
----
  1. RX 9070 / 9070 XT（RDNA4）+ AMD 26.10.07.02 预览驱动（正式驱动没有 Shader Model 6.10 的 wave matrix）。
     注意：Windows Update 会悄悄把预览驱动换成正式驱动（带在系统更新里），之后画面会写 "INIT FAILED"。
     重装预览驱动即可；要防再犯，组策略里禁止 Windows Update 带驱动（注册表
     HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate 建 DWORD ExcludeWUDriversInQualityUpdate=1）。
  1b. Windows 开发人员模式必须打开（设置 -> 系统 -> 开发者选项 -> 开发人员模式）。插件靠 D3D12EnableExperimentalFeatures
      打开实验性着色器模型，这个调用只在开发人员模式下成功；关着的话插件初始化就停在第一步，画面永远是 Magpie 自己的 FSR3。
      判断：DLSS5-AMD\logs\native-submission-order.txt 里 "sdk721_before_device ... experimental=" 后面不是 00000000 就是它。
      不需要装 SM 6.10 编译器、HIP、任何 SDK：着色器已经编好在包里。
  2. 本包已含 Magpie 实验分支（https://github.com/SAOG0721/Magpie）。
  3. 显示器分辨率不限，但游戏窗口必须是 1920x1080 无边框，Magpie 缩放选"原始尺寸"（不放大）。
     网络只认 1920x1080 进、1920x1080 出。尺寸不对时画面左上角会写一行
     "DLSS5-AMD: INPUT MUST BE 1920X1080 (NOW 2560X1440)" 之类的提示（0.12 起），看到它就去改游戏窗口 / Magpie 缩放模式。

安装（整包版：Magpie 本体已经在里面，解压即用）
----
  1. 解压到任意目录（路径别带中文），运行 Magpie.exe。配置是便携模式（config\config.json 随包），已经预设好：
     效果组 "DLSS5-AMD"（FSR3_SR + XeSS 帧生成 ZeroMV，光流 AMDOF）、缩放"原始尺寸"、重复帧检测已关。
     0.13 起效果组里挂了 XeSS 帧生成（Intel 的跨厂商 FG，不需要游戏给运动向量）：网络出 28～30 帧，
     插到 55～60 显示。代价是多一帧延迟、网络本身慢 20% 左右（光流和 FG 抢 GPU）。不想要就在效果组里把
     XeSS_FrameGeneration_x2_ZeroMV 删掉。Magpie 效能分析器（快捷键见设置）里"帧率（总/真实）"两个数就是插帧后/网络真实。
     如果你自己改了配置，要保证：效果组里 FSR3 -> FSR3_SR 在第一个、缩放选"原始尺寸"、设置里重复帧检测选"从不"。
  2. 游戏：显示模式无边框窗口，1920x1080。
  3. 在游戏里按 Magpie 的缩放热键（默认 Alt+Shift+A）激活。前 3~5 秒是网络初始化（权重在 Magpie 启动时已经预读进内存，
     着色器编译结果缓存在 DLSS5-AMD\native-game-tiled-assets\shader-cache\，第一次启动会多几秒），这段时间画面是 Magpie 自己的 FSR3，
     左上角写着 "DLSS5-AMD: INITIALIZING..."；接管后左上角变成 "DLSS5-AMD 37 FPS (26.9 MS)"，就是网络自己的帧率
     （数字至少间隔 3 秒刷新；字条复用并随网络输出提交；不想看就把 native-game-flags.txt 里的 DLSS5_SHOW_FPS=1 删掉）。
     如果提示变成 "INIT FAILED - SEE DLSS5-AMD\LOGS"，八成是开发人员模式没开或驱动不对，看上面"需要"一节。
     再按一次热键停止缩放就是对比。

已知
----
  - 实际帧率随游戏和场景变化；网络一般约 30 fps，启用插帧后显示帧率约为两倍。
  - 输入是显示用的 8 位 sRGB 图（不是游戏内钩子那种线性 HDR 场景色）；插件按 sRGB 直通处理（DLSS5_CODEC_SRGB=1），
    亮度和原图一致。0.09 里暗部皮肤上偶尔闪的 8 像素方块在 0.10 修掉了（硬件 FP8 转换对超范围值不饱和、出 NaN，现在进矩阵前夹到 ±448）。
  - 运动向量来自 Magpie 的光流估计（效果参数 Optical Flow Method 选 AMDOF）；光流在平坦暗部会给出几万像素的垃圾向量，
    插件把超过 64 像素的向量当静止处理（DLSS5-AMD\native-game-flags.txt 的 DLSS5_MOTION_MAX_PX），否则会出现黑色/粉色的方块闪烁。
  - 强度：DLSS5-AMD\native-game-flags.txt 里加一行 DLSS5_STRENGTH=<细节>,<颜色>（各 0～1，默认 1,1 = 网络结果全用；
    0.5,1 就是细节一半原图一半、颜色修正全用）。这是 NVIDIA 面板里"强度"那个滑杆对应的两个混合系数；改完重启 Magpie 生效。
    风格预设没有：DLL 里的其他网络预设没有提取，本包只有捕获时那一套权重。
  - 停止缩放再激活，插件会重新接管（需要重新初始化）。
  - 日志：DLSS5-AMD\logs\native-game-oneshot.txt（初始化）、native-submission-order.txt（每帧观察）。
  - 屏幕提示不想要：DLSS5-AMD\native-game-flags.txt 里加一行 DLSS5_NOTICE=0；帧率不想看：删掉 DLSS5_SHOW_FPS=1。
  - F6 是本插件的开关键（全局）；如果同一台机器上游戏里也装了本插件的游戏版，两边会一起切。

卸载
----
  整个目录删掉即可，不写注册表、不碰 AppData。

来源
----
  https://github.com/lmxxf/dlss5-on-amd-9070xt-porting（源码、每个 tag 的改动、开发记录）
