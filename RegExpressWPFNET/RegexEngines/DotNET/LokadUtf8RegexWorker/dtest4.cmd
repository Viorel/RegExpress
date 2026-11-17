@echo { "pattern" : "a+", "text" : "abcaa" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
@echo { "pattern" : "(a)+", "text" : "abcaa" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
@echo { "pattern" : "(a)+(b)+", "text" : "ab aabbb" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
