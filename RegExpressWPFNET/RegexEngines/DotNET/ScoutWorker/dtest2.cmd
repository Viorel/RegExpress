@echo { "pattern" : "(.)(?<n>.)", "text" : "abcd" } | "bin\Debug\net10.0\ScoutWorker.exe"
@echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad" } | "bin\Debug\net10.0\ScoutWorker.exe"
