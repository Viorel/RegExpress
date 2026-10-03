@echo { "pattern": "(.)",    "text": "a",            "flags": { } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "(.)",    "text": "\uD83E\uDD68", "flags": { "unicode": false } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "(.)",    "text": "\uD83E\uDD68", "flags": { "unicode": true } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "(....)", "text": "\uD83E\uDD68", "flags": { "unicode": true } } | zig-out\bin\RegexWorker.exe