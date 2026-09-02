@echo { "pattern" : ".", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "(.)", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "(.)(z)?", "Text" : "az", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "(.)(z)?", "Text" : "aq", "FindAll" : true } | OnigmoGoRegexpWorker.exe
