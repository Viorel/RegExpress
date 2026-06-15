@echo { "pattern" : ".", "text" : "abc", "flags": "gu", "func" : "exec"     } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : ".", "text" : "abc", "flags": "gu", "func" : "matchAll" } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "(.)", "text" : "abc", "flags": "g", "func" : "exec"     } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "(.)", "text" : "abc", "flags": "g", "func" : "matchAll" } | "QuickJs\qjs.exe" QuickJsWorker.js
