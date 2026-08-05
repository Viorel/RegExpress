@echo { "struct" : "RegexBuilder", "pattern" : "(.)(?<n1>.)(.)(?<n2>.)", "text" : "abcd", "options" : { "unicode" : true } } | ".\target\release\RustRegexWorker.exe"
