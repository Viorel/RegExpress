@echo { "pattern" : "a+", "text" : "aaa", "flags": "g", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "a++", "text" : "aaa", "flags": "g", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "a+", "text" : "aaa", "flags": "gC", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "a++", "text" : "aaa", "flags": "gC", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
