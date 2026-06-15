@echo { "pattern" : ".",       "text" : "abc", "flags": "g", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : ".",       "text" : "abc", "flags": "g", "func" : "matchAll" } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "(.)",     "text" : "abc", "flags": "g", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "(.)",     "text" : "abc", "flags": "g", "func" : "matchAll" } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "(?<n>.)", "text" : "abc", "flags": "g", "func" : "exec"     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "(?<n>.)", "text" : "abc", "flags": "g", "func" : "matchAll" } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
