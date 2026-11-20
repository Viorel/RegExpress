@rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo { "pattern" : ".", "text" : "x\u00EEy", "func" : "exec", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
@rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo { "pattern" : ".", "text" : "x\uD83E\uDD68y", "func" : "exec", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
