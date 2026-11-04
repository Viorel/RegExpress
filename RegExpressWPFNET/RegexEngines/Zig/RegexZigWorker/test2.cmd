@echo { "pattern": "(a)(b?)(c)?(d)", "text": "ad" } | "zig-out\bin\RegexZigWorker.exe"
@echo { "pattern": "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text": "adabcd" } | "zig-out\bin\RegexZigWorker.exe"
