@echo { "pattern": "(.)", "text": "a", "flags": { } } | ZigRegexWorker.exe
@echo { "pattern": "(.)", "text": "\uD83E\uDD68", "flags": { "unicode": false } } | ZigRegexWorker.exe
@echo { "pattern": "(.)", "text": "\uD83E\uDD68", "flags": { "unicode": true } } | ZigRegexWorker.exe
@echo { "pattern": "(....)", "text": "\uD83E\uDD68", "flags": { "unicode": true } } | ZigRegexWorker.exe