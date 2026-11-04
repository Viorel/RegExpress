@echo { "pattern": ".", "text": "abc" } | "zig-out\bin\RegexZigWorker.exe"
@echo { "pattern": "(.)", "text": "abc" } | "zig-out\bin\RegexZigWorker.exe"
@echo { "pattern": "(.)(?<a>.)(.)(?<b>.)", "text": "abcd" } | "zig-out\bin\RegexZigWorker.exe"
