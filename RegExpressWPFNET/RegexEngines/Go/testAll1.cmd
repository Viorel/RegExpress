@echo LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo regexp
@echo { "pattern" : ".", "Text" : "x\u00EEy" } | RegexpWorker\RegexpWorker.exe
@echo regexp2
@echo { "pattern" : ".", "Text" : "x\u00EEy" } | Regexp2Worker\Regexp2Worker.exe
@echo rexa
@echo { "pattern" : ".", "Text" : "x\u00EEy" } | RexaWorker\RexaWorker.exe
@echo coregex
@echo { "pattern" : ".", "Text" : "x\u00EEy" } | CoregexWorker\CoregexWorker.exe
@echo ---
@echo PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo regexp
@echo { "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | RegexpWorker\RegexpWorker.exe
@echo regexp2
@echo { "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | Regexp2Worker\Regexp2Worker.exe
@echo rexa
@echo { "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | RexaWorker\RexaWorker.exe
@echo coregex
@echo { "pattern" : ".", "Text" : "x\uD83E\uDD68y" } | CoregexWorker\CoregexWorker.exe
