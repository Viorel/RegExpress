@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad" } | "regexpworker.exe"
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad" } | "regexpworker.exe"
