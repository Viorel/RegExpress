rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo regexp
@echo { "package": "regexp",  "pattern" : ".", "Text" : "x\u00EEy" } | GoWorker.exe
@echo regexp2
@echo { "package": "regexp2", "pattern" : ".", "Text" : "x\u00EEy" } | GoWorker.exe
@echo rexa
@echo { "package": "rexa",    "pattern" : ".", "Text" : "x\u00EEy" } | GoWorker.exe
@echo coregex
@echo { "package": "coregex", "pattern" : ".", "Text" : "x\u00EEy" } | GoWorker.exe
@echo ---
rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo regexp
@echo { "package": "regexp",  "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | GoWorker.exe
@echo regexp2
@echo { "package": "regexp2", "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | GoWorker.exe
@echo rexa
@echo { "package": "rexa",    "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | GoWorker.exe
@echo coregex
@echo { "package": "coregex", "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | GoWorker.exe
