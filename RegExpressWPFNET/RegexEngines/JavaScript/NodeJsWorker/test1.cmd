@echo { "pattern" : ".(.)", "text" : "abcde", "flags": "g", "func" : "exec" } | node NodeJsWorker.js
@echo { "pattern" : ".(.)", "text" : "abcde", "flags": "g", "func" : "matchAll" } | node NodeJsWorker.js
