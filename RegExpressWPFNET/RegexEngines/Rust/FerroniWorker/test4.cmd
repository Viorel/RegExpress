@echo { "use_builder" : false, "pattern" : "(a)(b?)(c)?(d)", "text" : "ad x ad", "options" : { } } | ".\target\release\FerroniWorker.exe"
@echo { "use_builder" : false, "pattern" : "(a)(?<b>b?)(c)?(?<d>d)", "text" : "ad x ad", "options" : { } } | ".\target\release\FerroniWorker.exe"
@echo { "use_builder" : false, "pattern" : "(?<n1>.)(.)(?<n2>.)", "text" : "abc", "options" : { } } | ".\target\release\FerroniWorker.exe"
@echo { "use_builder" : false, "pattern" : "(?<a>a)|(?<a>b)", "text" : "ab", "options" : { } } | ".\target\release\FerroniWorker.exe"
