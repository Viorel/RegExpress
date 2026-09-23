@echo { "pattern" : "abc", "text" : "abc", "options" : {  } } | ".\target\release\DerivreWorker.exe"
@echo { "pattern" : "abc", "text" : "abcd", "options" : {  } } | ".\target\release\DerivreWorker.exe"
@echo { "pattern" : "abc(?P<stop>.*d)", "text" : "abcd", "options" : {  } } | ".\target\release\DerivreWorker.exe"
@echo { "pattern" : "[abx]*(?P<stop>[xq]*y)", "text" : "axxxxxy", "options" : {  } } | ".\target\release\DerivreWorker.exe"

