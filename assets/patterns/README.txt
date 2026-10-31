放在这里的 .pat 填充图案文件（AutoCAD 格式）会在启动时载入，与内置图案同名的以文件为准。
文件可以是 UTF-8 或 GBK（中文版 AutoCAD、天正自带的 .pat 是 GBK）。
内置图案为公制（单位 mm）；天正自带的图案（钢筋混凝土、加气混凝土等）也已编进程序，不用再放天正的 acad.pat。
两者都在 src/Scene/HatchPatternLibrary.inc。
