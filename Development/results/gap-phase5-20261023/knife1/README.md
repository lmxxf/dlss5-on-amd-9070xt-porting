# 第五阶段刀1：C512成对转换与跨半波广播

第四阶段生产为A。正式COMGR重编baseline整HSACO与生产相同，RTC_EXTRA_OPTS为空。三个独立候选：pair、perm、combined。四臂共108帧（三档三图样每臂27帧）Gold通过。所有候选已完成相应实测；最终仅pair-only进生产，perm/combined停止。

|目标w16核|总指令|VALU|cvt_pk_fp8|ds_bpermute|permlanex16|wait_dscnt|总wait|
|---|---:|---:|---:|---:|---:|---:|---:|
|baseline|2483|1367|144|16|0|45|136|
|pair|2104|1025|72|16|0|45|137|
|perm|2542|1438|144|0|16|31|122|
|combined|2175|1084|72|0|16|31|121|

四臂VGPR188、LDS6144、private0；WMMA404/VMEMload168相同。combined的perm/or3各52→16、lshl121→37、and68→41，并非全部清零。113Fn中仅compact与compact_w16代码改变，其余111原样。资源描述符的rsrc3变化另存resource-differences.json，不误说描述符逐字一致。

完整机器码计数见comgr-isa.json、isa-summary.json；原始COMGR输入/身份与patch已留。独立语义/历史查重见review/README.md：本次具体候选没有历史GPU结论，但不能说以前从未做过pack优化。GPU执行15秒硬超时；COMGR是CPU编译，单独180秒墙钟且同样持锁/游戏与磁盘守门，首次统一15秒编译超时原回执保留。

## 组合初测完成，暂不采纳

三档3轮ABBA：9006.424363→6.404209ms（−20.154µs），10888.785306→8.777113ms（−8.193µs），11529.076059→9.056411ms（−19.648µs）。900/1152三轮mean全改善，1088R1+6.108µs；九轮p99改善。

同1088纯NN：8.360377→8.345059ms（−15.318µs），但pooled p99 8.555312→8.739918ms（+184.606µs）；R3mean+0.018µs，R2/R3 p99变差。保留所有样本，不视为稳定采纳门。接着做pair/perm单项1088各3轮ABBA，判断是否组合互抵。详combined-statistics.json与pure-statistics.json。

## 单项1088拆分与独立探针

pair-only：8.828124→8.816423ms（−11.701µs），三轮均值−5.045/−10.024/−20.035µs；pooled p99−12.086µs，R1 p99+6.444µs原样保留。perm-only：8.824585→8.829454ms（+4.869µs），两轮均值回退，pooled p99+12.988µs。因此不采纳permlanex16单项/组合；pair尚需另外两档和纯NN门。

正式COMGR helper-probe两launch通过：1024wave的norm/broadcast逐bit及CPU lane-map、每条FP8路径524288点逐byte一致，覆盖所有half位码及FP32特殊值/随机位型，canary与输入不变。raw与正式模块在工作区knife1/helper-probe及compile-helper-probe，独立CPU复算另列review。仅full EXEC证明，不泛化到任意部分EXEC。

## pair-only三档补齐

9006.431656→6.408325ms（−23.331µs）；10888.828124→8.816423ms（−11.701µs）；11529.110355→9.091540ms（−18.815µs）。九轮mean全改善，三档pooled p99均改善；分轮p99反例：900R1+95.325µs、1088R1+6.444µs、1152R2+15.521µs/R3+12.824µs全部保留。另两档与1088各自为完整同批ABBA，不能跨批比较pair/combined绝对ms。pair-only纯NN最后验收中。

## 最终裁决：pair-only采纳

pair-only pure1088三ABBA：8.342062750→8.326007260ms，−16.055490µs，三轮mean−5.308/−21.578/−21.280µs；pooled p99−6.303µs，R1/R2尾+8.938/+7.381µs保留。正式源只加pair打包和该模块显式recipe开关，norm/W2不变。非W16三档18帧pairedGold通过；第一次baseline读输入失败是脚本误改fixture路径，修正后新case执行，原失败归档。

最终gfx1201与已测pair全部113函数机器码/资源相同，整.text/.rodata/.note亦逐字同；whole SHA不同仅符号/字符串/hash表。双架构80模块只更新2个c512-m32-mh，其余78保持；gfx1200仅编译/ELF，不冒实机。source尾空白清理与身份回执见final-recipe.json，正式产物见final-payload-identity.json。
