@echo { "pattern" : ".", "text" : "abc", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"
@rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo { "pattern" : ".", "text" : "a\u00EEc", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"
@rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo { "pattern" : ".", "text" : "a\uD83E\uDD68c", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"
