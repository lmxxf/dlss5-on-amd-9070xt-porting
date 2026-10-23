正式 COMGR 两模块共93个export，集合相同；仅 decoder_project2x_h16w_byteout_w、_w_bin、_w_bin_f8w16 三个函数的机器码改变，其余90个codeSHA全同，包括两个QK-fragment export及所有F32输出路径。完整AMDGPU kernel metadata仅前两个ByteOutput函数VGPR从85降至72，其他字段全同。目标W16仍166 VGPR、43 SGPR、2176B LDS、0 private。

W16目标静态 pk_fp8 转换208→192，不是104。候选192条具体为128条第二输入0与64条双有效输入；后者是成对输出打包，前者保留原F(skip)的FP8量化，仍对应128条反量化 cvt_f32_fp8。原H边界的160条f32→f16及160条f16→f32同样没有删除，坐标计算里的标量转换也保留。源级每16输出值从16次单元素pack改8次双元素pack；整个export包含两种输入/输出通道形状、full/tail与循环展开/控制流共享，不能用单出口比例乘整个静态函数计数，也不能把静态次数直接当每次dispatch执行次数。

正式目标3612→3446条指令、VALU1963→1812、wait427→411；WMMA192不变，perm20→0、or3 20→0，lshl_or仍28。这些是已生成代码证据；整网收益和数学相同仍由Gold与ABBA裁决。

完整可复算材料：all-export-check.py、all-export-isa.json、all-export-summary.json、all-export-metadata-diff.json。未改生产或访问目标机。
