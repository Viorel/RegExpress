@echo { "pattern" : "(.)(?<n>.)", "text" : "abcd" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"
@echo { "pattern" : "(.)(?<n>b)?", "text" : "abcd" } | "bin\Debug\net10.0\LokadUtf8RegexWorker.exe"