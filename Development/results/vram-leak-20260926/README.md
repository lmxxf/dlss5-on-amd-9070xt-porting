# HIP 导入的 D3D12 共享缓冲区永不归还——桥接层按档位池化（2026-09-26）

起因：3zwr1 AMDNR 0.3.3.2 更新日志称 AMD HIP 驱动对"导入并映射过的 D3D12 缓冲区"永不归还，每次改分辨率/档位漏约 100MB。

## 审计

两条路径共用 `Development/HIP/hip_d3d12_bridge.h`：
- 常规/Magpie add-on：`native_game_oneshot.h` 每个会话 `new NativeGameFrame` → `NativeHipNetwork::Create` → `bridge.Create`；重置/几何变化时整帧销毁重建。
- RE9 runtime：`src/LmxxfNrRuntime.cpp` 673/717 行每会话 `new D3D12Bridge`。

每个桥接 `Share` 三块共享缓冲区（input px×16B、history px×16B、output px×12B UAV，px=网络处理尺寸，只取 720/900/1080 三档）：CreateCommittedResource(SHARED) → CreateSharedHandle → hipImportExternalMemory → hipExternalMemoryGetMappedBuffer；`Release` 为 hipFree(mapped) → hipDestroyExternalMemory → CloseHandle → Release。另有共享栅栏的 hipImportExternalSemaphore（未单独测）。Network 自身 hipMalloc 正常。

## 实测（RX 9070 XT，驱动 32.0.31007.2048）

1. `leak_probe.cpp`（裸导入/映射/释放，按桥接顺序，1080/900/720/1080 轮换 40 次）：进程 DXGI 本地显存 11→3002 MiB、PrivateUsage 43→2924 MiB，每轮漏掉整套缓冲区（1080 档 ≈93 MiB，900 ≈65，720 ≈41）；跳过 hipFree 相同。hipFree/destroy 均返回 0。**驱动确实不归还。**
2. `bridge_probe.cpp`（真实 `D3D12Bridge` + 生产快路径 Options，每会话 Create → PrepareStagedKernels → 销毁，四档轮换 40 会话）：
   - `DLSS5_HIP_SHARED_POOL=0`（旧行为）：第 7→39 会话显存 2718→5052 MiB、私有 696→3042 MiB，**约 73 MiB/次切换**（三档平均）。`bridge-pool0.log`
   - 池化（默认）：第一轮三档后平台期，显存 ≈2530 MiB、私有 ≈310 MiB，第 7→39 会话不再增长。`bridge-pool1.log`

## 修复

`hip_d3d12_bridge.h`：进程级共享缓冲区池，键 = D3D12 设备 + 字节数 + 是否 UAV；`Share` 先取空闲项（连同已导入/映射的句柄复用），`Release` 只归还不销毁。上限 = 3 块 × 网络档位数（≤3），约 200 MiB。缓冲区执行后衰减到 COMMON，与新桥接的假设一致。`DLSS5_HIP_SHARED_POOL=0` 恢复旧创建/销毁。

## 验证

- 同进程 fresh vs 复用：三档输出回读哈希在池开/池关、首次/复用会话间全同（1080 `07e9abb5…`、900 `60b2145a…`、720 `dcfbe151…`）。
- NativeGameFrame 回归（生产 host 计数版，vit-proj-n64 同一模块集与脚本 `-CorrectnessOnly`）：全部 112 个输出 f16 文件与该集合此前记录的逐字节相同（`cmp-reg.ps1`）。单会话进程内池不复用，此回归证明非回归；复用正确性由上一条证明。
- add-on 重编 `5950fe20…`（含 auto-tier/wave-owned/C512/ViT 投影，同 0.31 源 + 本修复），部署包 `Development/deployments/vram-pool-20260926`（AMD `D:\DLSSNR-Lab\vram-pool-20260926`，`install.ps1` 只换 add-on，预检 0.31 的 106ff3d0，`-RestoreBackup` 回滚），**未安装、未发包**。
- RE9 runtime：`bash Development/RE9/presr/build-runtime.sh`（需 /tmp/re9-upstream-bridge-review 快照；产物 /tmp/re9-presr-build/LmxxfNrRuntime.dll，已确认编译通过且含池）。未替换发布包内 runtime。

限制：共享栅栏信号量导入是否同样泄漏未单测（每会话一个，量级应很小）；游戏内切档位实测未做。
