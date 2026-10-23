# Up39/Up48实际路由诊断（隔离Frame）

两host在`/home/lmxxf/work/dlss5-gap-phase6-20261023/knife2/up-route/`，参数完全保留benchmark_live_capture：ASSETS FLAGS INPUT PREFIX frames temporal MODULES reset_every edges_only seed pattern。标准host输入1296×720；free1072输入1920×1072。root可按七个既有profile各1–3帧执行。

唯一差异在Run最终映射之后、真正Fn解析完成且即将launch时打印：

`UP_ROUTE small_active=... model=... W=... H=... ic=... oc=... module=... kernel=...`

只匹配decoder_project2x前缀；C32命名映射等其它分支不打印。decoder没有paired_contract替换，日志是本次launch的真实选中名称。ic1024/oc512辨Up39，ic512/oc256辨Up48；ByteOutput由实际名字的byteout辨认。small_active可剔除Small1080构造期Original预热，不能仅按整个进程模式归属。

没有observer/profile/event/newgetter、没有改设备代码或选路，输入/完整Frame/readback行为保持。printf会改变host节奏，绝不使用本诊断输出的wall_ms做性能结论。生产头未改，当前基线是限定QK生产header6461…30b8；补丁、source/build SHA在本目录。
