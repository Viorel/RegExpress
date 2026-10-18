@echo LATIN SMALL LETTER I WITH CIRCUMFLEX (U+00EE), utf-8: C3 AE
echo {"pattern": ".", "text": "x\u00EEy", "options" : { "FindAll" : true } } | adaworker.exe
@echo PRETZEL (U+1F968), \uD83E\uDD68, utf-8: F0 9F A5 A8
echo {"pattern": ".", "text": "x\uD83E\uDD68y", "options" : { "FindAll" : true } } | adaworker.exe
