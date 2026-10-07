PR12锁定3ef0b6e，完整接口合并保留作者祖先，FastHistory默认0。修补addon启动/热切MP1独立守门、首次普通RGBinput UAV→SRV默认回归、未知MV有效区拒绝；genericaux MP1/2/3接口不缩回。新算法是F32反馈与启发式的近似，不是041a/NV逐位复刻，成本/游戏抑闪未测。

本地scope与源状态回归检查、最终runtime MinGW编译通过。1392原接口套件MSVC/WARP/AMD全通过（legacycodec297b字节同、ownerfault、桥/aux编译、5tap/depth/reset/黑输出/4Kcoverage、10queuedFrame不可变controls）；随后发现普通RGBinput首状态问题并修，14793只跑受影响addon编译/WARP/AMD含paddedMV negativefixture，单独通过，不冒最终全套重跑。详见receipt与日志。未做fullNNaux新export资产门/游戏观感/性能/gfx1200hardware。无安装/包/config更改。
