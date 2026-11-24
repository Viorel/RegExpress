@echo { "pattern" : "(a)(b?)(c)?(d)", "Text" : "ad" } | Regexp2Worker.exe
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "Text" : "ad" } | Regexp2Worker.exe
