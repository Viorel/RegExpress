@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad" } | "bin\Debug\net11.0\DotNET11Worker.exe"
@echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad" } | "bin\Debug\net11.0\DotNET11Worker.exe"
