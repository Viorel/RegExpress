@echo { "use_builder" : false, "pattern" : "(?<a>a)(?<b>b?)(?<c>c)?(?<d>d)", "text" : "ad", "options" : { } } | ".\target\release\RustFancyWorker.exe"
@echo { "use_builder" : true, "pattern" : "aa|aaa", "text" : "aaa", "options" : { "leftmost_longest" : false } } | ".\target\release\RustFancyWorker.exe"
@echo { "use_builder" : true, "pattern" : "aa|aaa", "text" : "aaa", "options" : { "leftmost_longest" : true } } | ".\target\release\RustFancyWorker.exe"
