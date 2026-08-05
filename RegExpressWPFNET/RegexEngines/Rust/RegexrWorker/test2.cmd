@set RUST_BACKTRACE=0
@rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo { "structure" : "RegexBuilder", "pattern" : "(?u).", "text" : "a\u00EEc", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
@rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo { "structure" : "RegexBuilder", "pattern" : "(?u).", "text" : "a\uD83E\uDD68c", "options" : { "jit" : false, "optimize_prefixes" : false } } | ".\target\release\RustRegexrWorker.exe"
