@echo { "pattern" : "(BAD", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : ")", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "\\", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
@echo { "pattern" : "(?zi)A", "Text" : "abc", "FindAll" : true } | OnigmoGoRegexpWorker.exe
