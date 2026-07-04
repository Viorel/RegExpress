@echo { "pattern": ".", "text": "abc", "flags": { } } | ZigRegexWorker.exe
@echo { "pattern": "B", "text": "abc", "flags": { } } | ZigRegexWorker.exe
@echo { "pattern": "B", "text": "abc", "flags": { "case_insensitive": true } } | ZigRegexWorker.exe