@echo { "cmd" : "m", "pattern" : ".", "text" : "abc", "options" : { "Global" : true } } | "bin\Debug\net10.0-windows7.0\DotNETWorker.exe"
@echo { "cmd" : "m", "pattern" : "(.).", "text" : "abcd", "options" : { "Global" : true } } | "bin\Debug\net10.0-windows7.0\DotNETWorker.exe"
