@echo { "pattern": ".", "text": "abc", "flags": { } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "B", "text": "abc", "flags": { } } | zig-out\bin\RegexWorker.exe
@echo { "pattern": "B", "text": "abc", "flags": { "case_insensitive": true } } | zig-out\bin\RegexWorker.exe