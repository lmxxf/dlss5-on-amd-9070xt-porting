# 2026-09-07 收工现场：正确画面慢速展示

本页优先于 README 中旧里程碑。不宣称完整移植目标已经完成。

## 20:14之后：用户恢复性能工作

最新QKV分块已通过完整五帧原版字节对照，暖轮2173.7783275→1856.0057175ms，证据release/native-network70-tiled-qkv/profile-validation.json。DLSS5_TEST_TILED_QKV=1显式启用；默认关闭，游戏安装版未改。注意ViT stage2是QKV，stage3才是attention，先前关于stage2为attention的方向已纠正。

最新完整整网共享FFWD已通过（session2674/PID1808 exit0）：五轮最终原版逐字节一致，暖轮2439.2734975→2173.7783275ms；encoder23_head降至106.53807ms。证据release/native-network70-shared-ffwd/profile-validation.json。游戏DLL未更新、游戏仍退出。接下来可检查ViT attention单线程ex[640]/quantized[640]暂存开销，尚未实施该优化。

后续目标更新为实际神经画面超过10fps。新增默认关闭DLSS5_TEST_SPLIT_FFWD=1共享前馈候选，小夹具五轮四阶段/输入切换通过，前馈暖均34.16767→0.65429ms，整核心36.07349→4.27147ms；注意力段1.47757→3.12796ms有回退。仅16×8样本，完整尺寸/整网尚待验证，不能外推游戏FPS。日志release/split-shared-ffwd/result.log与release/split-projection-tiled/profile.log。

用户要求暂以现有正确性为基线，继续优化。首个候选将NativeSplit的两段C512投影切成共享分块，默认关闭，仅DLSS5_TEST_SPLIT_PROJECTION=1启用。现成四阶段输入切换五轮通过，完整off/on/reset五轮最终字节也一致；暖轮2.710→2.439秒，约10%收益，证据release/native-network70-split-projection/profile-validation.json。代码已保留，尚未推广到游戏DLL。

为避免GPU计时争用，21632已正常Alt+F4退出，完整测试PID21952已exit0；当前游戏不在运行，已安装DLL与开关文件不变。下文“最后检查PID21632”描述封存时历史状态，不是现时进程。继续优化时以此段及worklog最新条目为准。

## 当前能做到什么

- AMD完整0～70层、1080p输入、1152处理高度、原版shift3，五轮history off/on/reset最终输出与独立原版逐字节一致。证据：`release/native-network70-tiled/profile-validation.json`。全网暖轮约2.71秒，不是实时。
- 旧真实游戏单帧PID34096/request1已独立原版重放验证一致，证据：`release/native-live-neural-34096/`。
- 当前连续展示DLL每帧重设history=0，游戏内连续处理/回写已运行；用户20:09确认“这个是对的，虽然只有2fps，主人公有一点黑”。这是用户观感确认，不替代所有新帧的数值核验。
- 43156正面像及21632展示首帧已读回，finite、alpha保持、RGB有变化；这些新帧独立原版整网对照尚未完成。连续历史反馈、时序画质、最终开关对照仍待完成。不能用每帧reset冒充完整temporal。

## Windows现场（amd9070）

最后检查PID21632仍Responding，持续render_complete；不要假定下次PID仍相同。

- 游戏：`C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64`。
- 自动载入DLL：`native-submission-order.addon64`，实际为神经渲染验证版，名称带observer的老日志字段不代表只读。
- 当前SHA256：`b6e42d355396a88a3f4181556240497a9514dfb9827e8aeee517b5aa322c0903`。
- 资产：`D:\DLSSNR-Lab\native-game-tiled-assets`；噪声表：`D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32`。当前不是自包含发行包，不能只复制DLL到另一台机器。
- 开关文件：`D:\DLSSNR-Lab\continuous-reset-preview.txt`。存在即自动慢速反复处理；初始化本次约195秒，每次处理约4秒，屏幕FPS计数不等于神经吞吐。
- 暂停展示：把上述开关文件改名为`.disabled`，当前在途帧完成后停止自动请求；还原文件名即可继续。无须杀进程或改存档。
- 回退到手动单帧：先正常退出游戏，备份当前DLL，再用同目录`native-submission-order.addon64.before-continuous-reset`替换DLL，同时禁用上述开关。该备份SHA为`61e731057b39233b8e9c55cdd6d08791c6a7196d99d6cf44e2b812f18552b1cb`。不要在游戏运行中替换DLL。
- 旧错误渲染器`dlss5-1080p-runtime.addon64.before-order-probe`仍禁用，不要恢复成自动加载后缀。
- 正常启动Steam游戏即可自动加载；现有任务`DLSS5GameLaunch`也可启动。初始化后自动处理，不需要再发请求文件。
- 日志：`D:\DLSSNR-Lab\logs\native-game-oneshot.txt`。确认当前PID的ready、render_begin、render_complete；render_failed不是成功。连续模式只保存request1前后原始帧，避免无限落盘。

## 本地封存与Git边界

`release/`已由仓库`.gitignore`中的`/release/`忽略。没有删除旧证据、权重或远端备份；没有把权重、图片、DLL提交到Git。

封存目录：`release/checkpoints/2026-09-07-correct-game-preview/`：

- `native-game-reset-preview.addon64`：当前安装版本，SHA见上。
- `native-game-neural-tiled-explicit.addon64`：手动单帧版，SHA见上。
- `native-network70-tiled.exe`：已通过整网测试的程序，SHA `cf9de2707d6ce5ea4699b41f55a9b15bb2b7739970cdf07058b57b30f591e319`。
- `run_nvidia_ui.uncommitted.ps1`：此前未提交的5090聚焦实验原样保留，未认定为正式修复。SHA `f267268583383d81b7e1a38ccb3568cc33d41098293bf29cb90b65a967f08698`。根目录同名脚本已恢复HEAD内容；需要续该实验时对比这份封存，不要以为改动丢失。

其余大文件保留原位，不复制多份GiB权重：

- `release/native-live-tiled-43156/`：正面像before/after及预览，原始线性数据预览不是最终swapchain截图。
- `release/native-live-continuous-21632/`：连续展示首帧、日志快照和被终端遮挡的桌面截图；该截图不能作为完整画质证明。
- `release/native-network70-tiled/`与`release/native-network70-profile/`：候选与基线输出、计时、验证报告。
- `release/native-color-frame/`、`release/native-temporal-valid1080/`等原版参考及权重保持原目录。
- 远端`D:\DLSSNR-Lab\live-reference-43156-1`已生成该帧原版encode；尚未完成其余原版重放。

## 恢复工作

1. 先看本页与porting-worklog最新段，再查Git状态、实际游戏PID和当前DLL散列，不重启仍存活的测试。
2. 用户20:14重新授权性能优化，暂沿用现有正确性基线；仍保留回归，不以牺牲神经输出换FPS。新帧独立核验与历史反馈未完成项保留，不改写为已完成。
3. 构建当前展示版：`bash build_native_game_verification.sh /home/lmxxf/work/tmp-test/minhook.KmbJvO/repo /tmp/reshade680.MNSVvW/include OUTPUT.addon64 --tiled`。先确认外部MinHook/ReShade路径仍存在；DLL已封存，不依赖临时二进制存活。
4. `deploy_native_tiled_verification.ps1`是旧手动版的一次性部署脚本，硬编码旧SHA且拒绝覆盖备份；不要拿它重装当前展示版。当前展示版SHA/回退方法以本页为准。
5. 不推送/发布旧网盘包作为完成版。完整目标保持未完成。
