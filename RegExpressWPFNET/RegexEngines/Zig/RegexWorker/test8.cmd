@echo { "pattern": "(.)", "text": "abc",            "flags": { "unicode": true } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "(.)", "text": "a\u00EEc",       "flags": { "unicode": true } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "(.)", "text": "a\uD83E\uDD68c", "flags": { "unicode": true } } | zig-out\bin\RegexWorker.exe
