@echo { "pattern" : "\\b", "Text" : "ab cd", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "a?", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "b?", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "c?", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "x?", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
