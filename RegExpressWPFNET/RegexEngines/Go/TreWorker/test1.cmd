@echo { "Pattern" : ".", "Text" : "abc", "Options" : { "Extended" : true, "FindAll" : true } } | TreWorker.exe
@echo { "Pattern" : "(.)", "Text" : "abc", "Options" : { "Extended" : true, "FindAll" : true } } | TreWorker.exe
@echo { "Pattern" : "b?", "Text" : "abc", "Options" : { "Extended" : true, "FindAll" : true } } | TreWorker.exe
@echo { "Pattern" : "(.)", "Text" : "abc", "Options" : { "Extended" : true, "NoSub" : true, "FindAll" : true } } | TreWorker.exe

