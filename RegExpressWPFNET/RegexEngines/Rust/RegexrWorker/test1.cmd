@echo { "structure" : "Regex", "pattern" : ".", "text" : "abc", "options" : { "jit" : true,  "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
@echo { "structure" : "Regex", "pattern" : ".", "text" : "abc", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
