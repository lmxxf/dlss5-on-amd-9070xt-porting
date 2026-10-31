# 0.46 正式发行回执

三包已交付于 `D:\給網友打包`。未上传网盘、未commit/push。

|文件|字节数|SHA256|
|---|---:|---|
|Magpie-DLSS5-AMD-0.46.zip|491,574,372|`d99cd557a3470d5cc26dda08d9490d4069e33c8f38e4df70ac7146011f360fbe`|
|OptiScaler-DLSS5-AMD-0.46.zip|521,772,408|`ead95c097b2e3f399bd39bfe5565e524e8cea2a0ca939a4a751910d94148de94`|
|OptiScaler-REFramework-DLSS5-AMD-0.46.zip|533,065,995|`d96108c350782ec1670fbf77031259987cccfeebdd55f2c1dd023befcc02dc2f`|

基线2f19eebc及发行文档dirty快照；五份文档与包内源码逐字节一致。源码SHA256：`23918d6a6f5f6450c505decd5518b6ec597189220a42d2bceb27de5121a377ec`。Round5 HIP80与hip/SHA256SUMS核同，使用指定O2宿主。

每包44 shader，共132 PASS；RE包runtime smoke exit0、15秒硬限。shader编译沿用0.45的180秒硬限。三个ZIP全部条目含清单流式SHA读回；默认MP0.5/MODE0/F9核同，无玩家custom/native。0.45原ZIP及sidecar核同回执后移入history；0.44 history SHA保持。

双游戏完整备份1502文件，169项安装与16配置最终读回通过，两路smoke exit0。见[安装回执](../../results/game-install-20261031/README.md)。最终无锁/实验进程。

过程异常如实保留：打包前执行策略/BOM修正；安装首次因缺两份架构清单只有165项，触发自动恢复；补清单的JSON数组包装预检失败未改游戏，修正后完整重做成功。末次审计修正ANSI历史路径读取为固定history+文件名后全部SHA通过。receipt的PowerShell value包装已规范化为普通数组，内容不变。
