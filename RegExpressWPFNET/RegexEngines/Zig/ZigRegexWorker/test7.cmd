@echo { "pattern": "(.)",    "text": "a",            "flags": { } } | zig-out\bin\ZigRegexWorker.exe
@echo { "pattern": "(.)",    "text": "\uD83E\uDD68", "flags": { "unicode": false } } | zig-out\bin\ZigRegexWorker.exe
@echo { "pattern": "(.)",    "text": "\uD83E\uDD68", "flags": { "unicode": true } } | zig-out\bin\ZigRegexWorker.exe
@echo { "pattern": "(....)", "text": "\uD83E\uDD68", "flags": { "unicode": true } } | zig-out\bin\ZigRegexWorker.exe