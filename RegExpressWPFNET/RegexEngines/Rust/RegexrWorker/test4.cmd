@echo { "use_builder" : true, "pattern" : "(.)(?<n1>.)(.)(?<n2>.)", "text" : "abcd", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
