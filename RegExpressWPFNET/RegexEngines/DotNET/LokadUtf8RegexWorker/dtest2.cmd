@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
