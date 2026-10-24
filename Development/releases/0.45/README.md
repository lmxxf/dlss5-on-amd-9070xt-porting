# 0.45 正式发行回执（2026-10-24）

**已完成安装与三包交付。** 公开镜像：[夸克](https://pan.quark.cn/s/0ccb3eff675b) · [Gofile](https://gofile.io/d/hmrcTjuS)。本地输出目录 `D:\給網友打包`；三个ZIP及同名`.zip.sha256`均已生成，旧0.44完整ZIP保留于history且原SHA不变。

|文件|字节数|SHA256|
|---|---:|---|
|Magpie-DLSS5-AMD-0.45.zip|491,900,423|`004bcae073fb76efa7f7dd1e88a8d38a4dd3c9884419bfb29722c00e2f07c473`|
|OptiScaler-DLSS5-AMD-0.45.zip|522,098,432|`96c12292cb11c8b7da026c012166745e0ca330f86d9ff16267a55163d9c367c5`|
|OptiScaler-REFramework-DLSS5-AMD-0.45.zip|533,387,409|`a46fee43bfc67e8fe367e3a4593ddd71dfbcd8afb9c91c330effa012e8857604`|

每包双架构80模块与冻结phase6产物一致；每包44项shader编译，共132项PASS。REFramework包内runtime smoke exit0、15秒硬限。全部ZIP条目（含SHA256SUMS.txt自身）均读回验证，默认MP0.5/MODE0/F9及各包AE默认值核同模板；不携带玩家custom/native，只带custom模板。

复用最终phase6宿主与模块，不重跑ABBA。源码附件对应git基线c24e3a97cdf596d0c997cee7deda91c981d520ce上的dirty生产快照，实际归档SHA为`3bcfa93db081f139b39170ed2d9c23da35b34759fbfb5e5593c709da971c829c`，未冒称基线提交已含新改动。末次修订仅两README的38→40；三stage恰改source tar、release.json、VERSION和SUMS四项，二进制/shader/config全部保持，故无需重复GPU smoke或shader编译，但新ZIP全文件重验。初版045 ZIP及sidecar保存在D:\DLSSNR-Lab\release-045\pre-docfix。

游戏安装另见[安装回执](../../results/game-install-20261024/README.md)：1502文件完整备份、169项SHA读回、16配置记录逐字保持、root与_storage_两路smoke exit0。

完整身份见receipt.json，独立复核在independent-review，原始打包/编译/smoke/重封/ZIP metadata在evidence；最终状态final-state.json确认无锁/无实验进程。三包在本地交付，未上传外部网盘或推送Git。
