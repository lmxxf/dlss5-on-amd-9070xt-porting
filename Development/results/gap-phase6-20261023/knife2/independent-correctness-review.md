# 刀2A独立正确性/基线复核（暂无收益结论）

仅本地CPU读取。A63帧七档三图样Gold按锁定参考hash同，42帧七档AE与对应旧路径逐帧hash及完整AE CSV同；6帧half-weight fallback双臂同，实际两个Up调用均为decoder_project2x_h16w_byteout_w_bin。合计111帧通过，未新增GPU。

21帧UP_ROUTE诊断按small_active/W/H及ic/oc过滤，剔除了Small1080的Original预热：**七档Up48均有ByteOutput实际命中**；Small900/720/free1088的Up39是F32输出_w_bin_f8w16，不受A改动；其它Up39为ByteOutput。没有把未命中Up39算成该档被优化，但各档Up48有真实作用点。

A已经rebase到接受的limited-QK基线，正式baseline deep SHAba1a29db…ea354，candidate529a77b6…89a51；实际文件重算匹配。93exports中只三个ByteOutput导出代码改变，90个其余函数codeSHA同，包含两个QK-fragment export和F32输出路径。两个half-weight导出VGPR85→72；FP8 W16目标仍166VGPR/43SGPR/2176LDS/private0。

目标pk_fp8静态208→192，不是减半为104：128个原skip量化及其反量化、160对half往返保持。仅输出打包局部16单元素→8成对；不能拿单出口比例乘整函数或整网。源与设备代码局限由all-export明细支持；实际净收益待84槽ABBA，不预写提升。

复算数据见independent-correctness-review.json。没有目标I/O、生产修改或数据筛选。
