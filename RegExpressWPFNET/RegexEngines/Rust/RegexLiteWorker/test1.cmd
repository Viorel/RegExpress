@echo { "use_builder" : false, "pattern" : ".", "text" : "abc", "flags" : "" } | ".\target\release\RustRegexLiteWorker.exe"
@rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo { "use_builder" : false, "pattern" : ".", "text" : "a\u00EEc", "flags" : "" } | ".\target\release\RustRegexLiteWorker.exe"
@rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo { "use_builder" : false, "pattern" : ".", "text" : "a\uD83E\uDD68c", "flags" : "" } | ".\target\release\RustRegexLiteWorker.exe"
