# AttExp 本地试玩候选（不是发布版）

目标：在《剑星》看自适应 ViT 复用的动态画面。网络和权重未重训；输入变化时的复用属于有损近似，ViT输入逐位相同时可无损延长缓存。

候选目录：`D:\DLSSNR-Lab\AttExp-preview`。当前仅暂存，不覆盖游戏。

完全退出《剑星》后安装：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\AttExp-preview\install-preview.ps1 -Action Install
```

进入游戏后，默认开启实验复用。**F8：实验复用 / 完整计算**，两边都有 DLSS5；F6 仍是整个 DLSS5 开关。已开启的 DLSS5 帧率行会附加 AE / EXACT，更新最多等3秒；原本隐藏的显示不会被强行打开。

先看转镜头、人物走过前景、暗处、菜单进出和切场景。变化大时会多重算，收益随场景变化；小于检测区域的变化仍可能漏判。实际游戏稳定性、物理F8按键及主观画质由这次试玩确认，离线数据不是承诺。

恢复原安装（同样先退出游戏）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\AttExp-preview\install-preview.ps1 -Action Restore
```

安装器会备份原 DLL、模块和配置，并校验文件；不改网络档位、原来的帧率显示偏好或其他游戏设置。
