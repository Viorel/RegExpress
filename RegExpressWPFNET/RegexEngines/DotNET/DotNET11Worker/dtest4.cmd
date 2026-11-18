@echo { "pattern" : "a+", "text" : "abcaa" } | "bin\Debug\net11.0\DotNET11Worker.exe"
@echo { "pattern" : "(a)+", "text" : "abcaa" } | "bin\Debug\net11.0\DotNET11Worker.exe"
@echo { "pattern" : "(a)+(b)+", "text" : "ab aabbb" } | "bin\Debug\net11.0\DotNET11Worker.exe"
