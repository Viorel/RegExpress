@echo { "pattern" : "(a)(b)(?<nc>c)(d)", "text" : "abcd", "options" : { "implicit_captures" : false } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : "(a)(b)(?<nc>c)(d)", "text" : "abcd", "options" : { "implicit_captures" : true } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad", "options" : { "implicit_captures" : true } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad", "options" : { "implicit_captures" : true } } | "target\release\RustResharpWorker.exe"
