@echo { "pattern" : "(.)", "text" : "abc", "options" : {  } } | ".\target\release\RustyExpressionsWorker.exe"
@echo { "pattern" : "(.)(?<n1>.)(.)(?<n2>.)", "text" : "abcd", "options" : { "CAPTURE_GROUP": false  } } | ".\target\release\RustyExpressionsWorker.exe"
@echo { "pattern" : "(.)(?<n1>.)(.)(?<n2>.)", "text" : "abcd", "options" : { "CAPTURE_GROUP": true  } } | ".\target\release\RustyExpressionsWorker.exe"
@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "adXXXad", "options" : {  } } | ".\target\release\RustyExpressionsWorker.exe"

