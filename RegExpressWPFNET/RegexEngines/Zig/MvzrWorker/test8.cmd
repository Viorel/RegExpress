@echo { "pattern": "(a)(b?)(c)?(d)", "text": "ad", "flags" : {  } } | "zig-out\bin\MvzrWorker.exe"
@echo { "pattern": "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text": "ad", "flags" : { } } | "zig-out\bin\MvzrWorker.exe"
