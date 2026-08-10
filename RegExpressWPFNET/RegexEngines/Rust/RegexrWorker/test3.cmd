@echo { "use_builder" : true, "pattern" : "(a)(x)?", "text" : "abc", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
@echo { "use_builder" : true, "pattern" : "(?<n1>a)(?<n2>x)?", "text" : "abc", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
