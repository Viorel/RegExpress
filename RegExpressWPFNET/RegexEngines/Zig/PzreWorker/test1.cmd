@echo { "pattern": ".", "text": "abc" } | "zig-out\bin\ZigPzreWorker.exe"
@echo { "pattern": ".", "text": "abc", "flags": { } } | "zig-out\bin\ZigPzreWorker.exe"
@echo { "pattern": ".", "text": "abc", "flags": { "is_debug": true } } | "zig-out\bin\ZigPzreWorker.exe"