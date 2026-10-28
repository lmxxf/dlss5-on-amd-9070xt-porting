# 刀4：完成

常规包模板scripts/optiscaler-regular.ini补[DlssNr] TransferStrength=1.0 / ColourStrength=1.0。使用0.45实际SetIni函数测试缺失节与旧1.5/1.1覆盖，两项通过。未修改已发布ZIP；未进行游戏画质实玩，不宣称已实测消除闪烁。

正式scripts/build-addon.sh --hip、scripts/build-runtime.sh与标准Frame O2构建成功；frame SHA同刀1最终已验二进制。最终载荷在../final-payload（忽略Git的交付产物，不是实验临时目录）。刀2/3均STOP，因此双架构80模块复用已验v0.45载荷，逐SHA同hip/SHA256SUMS，逐ELF metadata确认gfx1200/1201，附总表与分架构清单。gfx1200未实机验证。

独立实验目录runtime smoke首次缺模块清单导致Create失败，补完整HIP布局后因没有shaders导致PrepareFrame失败；按FindShaderDir实际规则补仓库shaders后exit0。发现该次使用实验上层旧配置，再显式传入最终配置补验，Small0.5/skip0/vit_stream3/MP1通过，15秒守门、锁释放。全部旧失败日志保留，不冒充最终通过。初次ELF目标字符串解析未兼容LLVM的unknown字段、分架构清单缺失均在CPU核验被发现，已修正并重验80项通过。

正式二进制身份见receipt.json，模块身份见modules-80.json；实验原CSV/命令/哈希保留，临时大二进制清理见cleanup.json。游戏/发行ZIP未动，无push。
