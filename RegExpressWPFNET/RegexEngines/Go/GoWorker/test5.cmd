echo { BAD | GoWorker.exe
echo { "package" : "regexp", "pattern" : "BAD(", "Text" : "abc" } | GoWorker.exe
echo { "package" : "regexp", "pattern" : ".", "Text" : "abc", "EnableDFA" : 1234 } | GoWorker.exe