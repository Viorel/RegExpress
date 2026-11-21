@echo { "pattern" : "a?", "text" : "abc", "flags": "g", "func" : "exec"     } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "a?", "text" : "abc", "flags": "g", "func" : "matchAll" } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "a?", "text" : "a\u00E9c", "flags": "g", "func" : "exec" } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "a?", "text" : "a\uD83E\uDD68c", "flags": "g", "func" : "exec" } | "QuickJs\qjs.exe" QuickJsWorker.js
