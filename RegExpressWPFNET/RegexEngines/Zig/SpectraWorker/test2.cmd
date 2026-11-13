@echo { "pattern": "(a)(b?)(c)?(d)", "text": "ad", "options" : { "dialect" : "ecmascript" } } | "zig-out\bin\SpectraWorker.exe"
@echo { "pattern": "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text": "ad", "options" : { "dialect" : "ecmascript" } } | "zig-out\bin\SpectraWorker.exe"
