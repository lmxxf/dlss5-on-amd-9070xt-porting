# 刀1最终独立读审

从原始 CSV、stdout/stderr、command/raw-hash 回执重新核算，未调用性能统计脚本代替复算，未新增实验。详细独立结果见 `final-review.json`。

**结论：刀1现有证据支持采纳。** 生产改动只有两处 compact 资格；最终新增1088/960限实际 Original，原1152行为保持。最终本地三exe SHA均与 `final-build-receipt.json` 相符，生产头与最终编译快照SHA也一致。最终Gold command实际使用 `final-frame.exe`，兼容candidate实际使用 `final-trace.exe`；性能仍是中间candidate，报告已明确区分，没有偷换产物。

## 性能复算

每档12槽、每槽240帧丢前80、每arm960帧，复算与 `statistics.json` 相符：

| 处理档 | baseline→candidate ms | 均值差 ms | pooled p99差 ms |
|---|---:|---:|---:|
|1600×960|6.548211771→6.477251667|−0.070960104|−0.028782|
|1920×1088|9.090362083→8.880000521|−0.210361563|−0.181314|
|1920×1152|9.167038646→9.160298333|−0.006740312|−0.130967|

900/1088支持有效收益。1152本来就走compact，均值按中性处理；R2/R3 p99分别退步0.043854/0.035559ms必须原样保留，不能以pooled改善宣称各轮无退化。以上是完整Frame墙钟，不与第一阶段纯NN GPU时间混用。

## Gold与回退覆盖

- 初始18个Gold任务×3帧=54帧，三几何×三图样×双arm；每帧finite、repeat，通过成对输出hash一致。
- 最终3档×3帧=9帧，final-frame输出与相应原基线真实HDR Gold同。
- 48个兼容任务×3帧=144帧，24对逐帧hash一致；default、FAST0、冷MP2、1→2→1热切、四种缺导出注入都实际执行。
- 默认dispatch：900 **165→157**，1088 **161→153**，1152 **153→153**。逐帧序号连续，无UNMAPPED，SP及any-order包装均在诊断源码内。
- FAST0/冷MP2实际compact关；热切实际mode为1/2/1，回到1时输出与初始1同。热MP2保留已选compact句柄是实际行为，不被误写成冷启动资格。
- 缺导出实际由GetFunction边界返回500，日志有真实模块handle；这是受控API失败注入，非物理删export。候选三几何各四case及旧1152各四case，共16个任务实际触发注入；旧1088/900原资格未命中，因此8个对应baseline任务自然无注入日志。单缺bstream/bout后字节链回退且输出同。

## 发现并修复的记录问题

`analyze-compat.py` 原先用错误标记 `GAP_HIDE` 统计 `injection_logged`，实际stderr标记是 `GAP_GETFUNCTION_INJECT`。已只修解析字符串并重新生成 `compat-results.json`，原始日志、CSV、exe、GPU数据完全未动。正确回退行为由原日志和输出已经证明，修复的是证据标签。

## 保留边界

大raw留远端，本次读审复核的是已生成hash回执及程序有限/重复检查，未另下载每份raw重hash。未测试新Small几何或gfx1200真机；新资格明确排除前者，原device gate保留后者回落。400-token三输入完整Gold通过仍不等于全输入数值定理，也未覆盖人为预种legacy-cache再改资格的非公开组合。最终资格收窄后只补Gold，不将已有性能批次伪称新exe重测。
