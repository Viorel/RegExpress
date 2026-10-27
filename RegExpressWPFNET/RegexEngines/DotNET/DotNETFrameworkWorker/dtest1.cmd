@echo { "cmd" : "m", "pattern" : ".", "text" : "abc", "options" : { "Global" : true } } | "bin\Debug\DotNETFrameworkWorker.exe"
@echo { "cmd" : "m", "pattern" : "(.).", "text" : "abcd", "options" : { "Global" : true } } | "bin\Debug\DotNETFrameworkWorker.exe"
