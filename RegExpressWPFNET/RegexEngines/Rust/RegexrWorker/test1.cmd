@echo { "structure" : "RegexBuilder", "pattern" : ".", "text" : "abc", "options" : { "jit" : true,  "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
@echo { "structure" : "RegexBuilder", "pattern" : ".", "text" : "abc", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
