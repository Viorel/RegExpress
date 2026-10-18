@echo { "pattern" : "(?@.)+", "text" : "abc", "options" : { "FindAll" : true } } | ".\target\release\RustyExpressionsWorker.exe"
::@echo { "pattern" : "(?@<n>.)+", "text" : ".", "options" : { "FindAll" : true } } | ".\target\release\RustyExpressionsWorker.exe"

