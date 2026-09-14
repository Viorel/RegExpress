@echo { "pattern": "(a)(b?)(c)?(d)", "text": "ad", "flags" : { "is_debug" : false } } | "zig-out\bin\Zoptia0regexWorker.exe"
@echo { "pattern": "(?<a>a)(?<b>b?)(?<c>c)?(?<d>d)", "text": "ad", "flags" : { "is_debug" : false } } | "zig-out\bin\Zoptia0regexWorker.exe"
