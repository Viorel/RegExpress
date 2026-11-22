@echo { "pattern": "(?<first>\\d)(\\d*)(?<last>QQQ)?", "text": "a1b23c456", "flags": { } } | zig-out\bin\ZigRegexWorker.exe
@echo { "pattern": "(a)(b?)(c)?(d)", "text": "ad", "flags": { "ecmascript" : true } } | zig-out\bin\ZigRegexWorker.exe
@echo { "pattern": "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text": "ad", "flags": { "ecmascript" : true } } | zig-out\bin\ZigRegexWorker.exe