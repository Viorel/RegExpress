@echo { "pattern" : "((a))*b", "text" : "b", "options" : { } } | ".\target\release\ReXileWorker.exe"
@echo { "pattern" : "(a?)(b)?c", "text" : "c", "options" : { } } | ".\target\release\ReXileWorker.exe"
@echo { "pattern" : "c(a?)(b)?", "text" : "c", "options" : { } } | ".\target\release\ReXileWorker.exe"
