@echo { "pattern" : "\\g<any>` (?(DEFINE)(?<any>.))", "text" : "ab`c", "flags": "gx", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
