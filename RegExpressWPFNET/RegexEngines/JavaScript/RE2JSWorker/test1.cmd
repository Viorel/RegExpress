@echo { "pattern" : ".", "text" : "abc", "flags": "" } | "..\QuickJsWorker\QuickJs\qjs.exe" RE2JSWorker.js
@echo { "pattern" : "(.)", "text" : "abc", "flags": "" } | "..\QuickJsWorker\QuickJs\qjs.exe" RE2JSWorker.js
@echo { "pattern" : "a(.)(?<n>.)(?<x>x)?", "text" : "abdcABCD", "flags": "i" } | "..\QuickJsWorker\QuickJs\qjs.exe" RE2JSWorker.js
