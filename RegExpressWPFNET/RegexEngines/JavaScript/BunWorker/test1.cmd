@echo { "pattern" : ".", "text" : "abc", "flags": "g", "func" : "exec" } | "bun-windows-x64-baseline\bun.exe" BunWorker.js
@echo { "pattern" : ".", "text" : "abc", "flags": "g", "func" : "matchAll" } | "bun-windows-x64-baseline\bun.exe" BunWorker.js
