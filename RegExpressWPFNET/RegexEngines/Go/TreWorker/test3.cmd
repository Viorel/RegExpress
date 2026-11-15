@rem LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
@echo { "Pattern" : ".", "Text" : "a\u00EEc", "Options" : { "Extended" : true, "FindAll" : true } } | TreWorker.exe
@rem PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
@echo { "Pattern" : ".", "Text" : "a\uD83E\uDD68c", "Options" : { "Extended" : true, "FindAll" : true } } | TreWorker.exe
