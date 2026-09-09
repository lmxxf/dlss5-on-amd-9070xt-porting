《剑星》(Stellar Blade) DLSS 5 网络 AMD 移植 —— 用户运行包 0.07（fast36）

这是把 DLSS 5（DLSSNR）的神经网络逐块移植到 AMD 显卡上的实验版本，不是 NVIDIA、AMD 或游戏厂商的官方产品。
源码与开发记录：https://github.com/lmxxf/dlss5-on-amd-9070xt-porting
公众号系列（中文）：微信搜索合集「DLSS5」

已测试环境
  Windows 11、AMD Radeon RX 9070 XT、《剑星》Steam 版、1920×1080 窗口、FSR 质量档（网络在 1920×1152 的内部网格上跑）。
  游戏内约 32～33 fps（FSR 原生约 60+）。其他显卡、分辨率、HDR 尚未验证；RX 7000 系列理论上不支持（需要 RDNA4 的 wave matrix 指令）。

必须先满足的两个条件
  1. AMD 预览驱动 32.0.31007.2048（支持 Shader Model 6.10 / wave matrix 的 DirectX 预览版驱动）。
     正式版驱动（如 32.0.31041.x）会拒绝本包的 shader，表现为进游戏后画面始终是原生 FSR，日志里 PSO 创建失败。
     注意 AMD 软件会通过两条计划任务（AMD Install Manager - Check For Updates / Install Updates）自动把驱动换回正式版，装好预览驱动后请在「任务计划程序」里禁用它们。
  2. 显存 16GB。网络本身占约 3.2GB，游戏贴图质量建议不高于「高」；显存不够时帧率会掉到 12～18 并且可能不恢复。

包内文件
  d3d12.dll                          ReShade 6.8 加载器（原版，未修改）
  dlss5-amd.addon64                  本移植的 DLL（.addon64 是 ReShade 的扩展名，不要改名）
  DLSS5-D3D12-721\                   微软 DirectX 12 Agility SDK 1.721 预览运行时（D3D12Core.dll），SM6.10 需要它
  DLSS5-AMD\                         网络权重、编译好的 shader、运行参数和日志目录（约 700MB）
    native-game-flags.txt              运行参数，一行一个开关；一般不用改
    native-game-tiled-assets\          权重（.f16/.f32）、shader（.cso/.hlsl）、噪声表（noise.f32）
    logs\                              运行日志写在这里
  ReShade-LICENSE.txt / MinHook-LICENSE.txt   第三方组件的许可
  SHA256SUMS.txt                     所有文件的校验和

安装
  1. 完全退出游戏。
  2. Steam → 剑星 → 管理 → 浏览本地文件，进入 SB\Binaries\Win64。
  3. 如果该目录已有 d3d12.dll 或其他 ReShade/mod 加载器，先备份；不要在不了解的配置上直接覆盖。
  4. 把本包里的全部内容（两个文件 + 两个文件夹）复制进 Win64。DLSS5-AMD 文件夹必须和 dlss5-amd.addon64 在同一目录。
  5. 游戏设置：1920×1080、AMD FSR 超分辨率（质量档），先关闭帧生成。只在这个设置上验证过。
  6. 从 Steam 正常启动。主菜单里不会有任何变化——网络只在 3D 场景开始渲染（读档之后）时接管 FSR 的超分步骤，
     第一次接管要读权重、编译几个 shader、建几百个管线，约 5～10 秒，这段时间画面是原生 FSR，之后自动切换。

怎么确认它在工作
  F6 循环切换：神经网络输出 / 原生 FSR 画面 / 左右对照。注意 F6 只切显示，后台网络仍在跑，不能用它比较关掉网络后的帧率。
  日志：DLSS5-AMD\logs\native-game-oneshot.txt 和 native-submission-order.txt。正常运行时 oneshot 日志里有持续增长的 render_complete。
  如果一直是原生画面，先看驱动版本（条件 1），再看 native-submission-order.txt 里有没有 pso 或 sdk721 的失败记录。

已知问题
  1. 换区、过场动画后帧率可能掉到 15～20 几秒，多数情况 5～30 秒内恢复；这是显存被游戏贴图挤到系统内存造成的，
     降低贴图质量或在游戏的 Engine.ini 里加 [SystemSettings] r.Streaming.PoolSize=6000 能缓解。
  2. 本包默认跳过了 3 个对画质影响最小的中间块换取约 1ms（对精确链 PSNR 40.7dB，肉眼看不出）。
     不想跳：在 native-game-flags.txt 里删掉 DLSS5_SKIP_BLOCKS 那一行。
  3. 只在 1920×1080 + FSR 质量档上验证过。其他分辨率可能直接不接管（画面保持原生 FSR）。
  4. 与其他 ReShade 插件、帧生成、HDR 的组合未测试。

卸载
  退出游戏后删除 d3d12.dll、dlss5-amd.addon64、DLSS5-D3D12-721、DLSS5-AMD 四项，恢复自己备份的文件。
  游戏更新或换版本后需要重新验证。

关于权重
  本包不含 NVIDIA 的 nvngx_dlssnr.dll。权重文件是从该 DLL 中提取后按块拆分并转成 f16 的数据（数值与原始 DLL 完全一致），
  仅供本移植加载使用。NVIDIA 对 DLSS 模型的权利不受本包影响；如权利人提出要求会立即撤下。

—— 靳岩岩（Kien），2026-09-10。协作：Claude（光之朱雀）、GPT（闇之朱雀）。
