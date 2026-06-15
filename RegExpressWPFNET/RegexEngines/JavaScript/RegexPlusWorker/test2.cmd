@echo { "pattern" : BAD_JSON     } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
@echo { "pattern" : "**BAD", "text" : "abc", "flags": "gd", "func" : "matchAll" } | "..\QuickJsWorker\QuickJs\qjs.exe" RegexPlusWorker.js
