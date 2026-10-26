@echo { "pattern" : ".", "text" : "abc" } | "bin\Debug\net10.0\FuzzyRegexWorker.exe"
@echo { "pattern" : "(.).", "text" : "abcd" } | "bin\Debug\net10.0\FuzzyRegexWorker.exe"
@echo { "pattern" : "(.)+", "text" : "abc" } | "bin\Debug\net10.0\FuzzyRegexWorker.exe"