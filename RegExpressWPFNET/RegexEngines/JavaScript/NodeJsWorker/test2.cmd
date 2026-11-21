@echo { "pattern" : "a?", "text" : "abc", "flags": "g", "func" : "exec" } | NodeJsWorker.exe
@echo { "pattern" : "a?", "text" : "abc", "flags": "g", "func" : "matchAll" } | NodeJsWorker.exe
