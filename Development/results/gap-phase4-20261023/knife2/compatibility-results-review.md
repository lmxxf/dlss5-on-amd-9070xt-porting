# public Frame兼容完成：108帧通过，MODE1按真实idle语义校准

仅本地分析原有证据，未目标操作、未改生产、未重跑GPU。

最终 `compat-analyze.py` 完整执行通过：**7对、14任务、108逐帧检查**。三档public switch核实际DIRECT序列、处理几何Small→Original→Small、两次资源重建；Small末回环与各模式内重复同，双arm所有同帧hash一致。三档AE1的Small gain、实际reuse与完整AE decision CSV配对通过。MODE1两臂真实31块Small、自己的head/blend、静态guide fallback及暖历史实际出现。

原分析器把MODE1固定写为[0,1,1,0,1,1]，未考虑现有250ms idle reset，因而对baseline发出解析断言。CSV首帧分别**864.3142ms / 830.7558ms**，两者都超过250ms；日志真实序列均为**[0,0,1,0,1,1]**，且第二帧CSV reset=0，说明不是fixture人为加reset。源码Prepare在固定idle超时后会清历史；这与首帧冷准备耗时一致。

没有刷数据凑旧断言。修正仅额外接纳这一具体序列，并同时要求：首帧wall>250ms、显式reset仍[1,0,0,1,0,0]、帧0/1/3冷输出hash同、帧2/4/5实际use_history1、两臂history序列及逐帧hash完全相同。其它异常reset仍失败。实际原因/首帧耗时已写进结果JSON。

旧断言及修改前脚本SHA、源机制和观测保存在 `compat-analysis-initial-assertion.json`；原GPU日志/CSV/raw回执未变。`compat-results.json`保完整各case证据，本报告不将MODE1跨帧变化误当数学误差，也不称有真实MV/depth重投影（本次明确static fallback）。兼容门不代性能门，wall值仅用于解释idle行为。
