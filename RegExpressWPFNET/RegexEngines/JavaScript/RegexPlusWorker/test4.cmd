@echo { "pattern" : "[[ab]&&[bc]]", "text" : "abc", "flags": "", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "[[ab]&&[bc]]", "text" : "abc", "flags": "v", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
