echo { "pattern" : "(?<n>x)|(?<n>y)", "text" : "xyz", "flags": "gd", "func" : "exec" } | "SpiderMonkey\SpiderMonkey.exe" --enable-regexp-duplicate-named-groups SpiderMonkeyWorker.js
