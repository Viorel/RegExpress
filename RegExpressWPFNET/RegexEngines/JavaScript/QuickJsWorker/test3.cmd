@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad", "flags": "g", "func" : "matchAll" } | "QuickJs\qjs.exe" QuickJsWorker.js
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad", "flags": "g", "func" : "matchAll" } | "QuickJs\qjs.exe" QuickJsWorker.js
