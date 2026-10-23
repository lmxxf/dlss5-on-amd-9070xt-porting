重封首次遇WinPS默认ANSI读取无BOM UTF8 release.json（中文路径）导致JSON解析失败，原ZIP不变；以已SHA验证的初版ZIP恢复stage四项，再显式UTF8读取重封成功。恢复时ZIP成员路径需统一反斜杠/斜杠，已修正。原失败日志保留reseal-attempt1*。这段仅操作发行stage/ZIP，不触已安装游戏。
