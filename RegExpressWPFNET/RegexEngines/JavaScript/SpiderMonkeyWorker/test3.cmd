echo { "pattern" : "(?i).(.)", "text" : "abcdef", "flags": "gd", "func" : "exec" } | "SpiderMonkey\SpiderMonkey.exe" --enable-regexp-modifiers SpiderMonkeyWorker.js
