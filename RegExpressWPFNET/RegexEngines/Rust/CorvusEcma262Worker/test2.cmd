@echo { "pattern" : "(.).(x)?", "text" : "abcd", "options" : { } } | ".\target\release\CorvusEcma262Worker.exe"
@echo { "pattern" : "(.)(.)(?<n1>.)(?<n2>x)?", "text" : "abcde", "options" : { } } | ".\target\release\CorvusEcma262Worker.exe"
@echo { "pattern" : "a((?<name1>b))", "text" : "ab", "options" : { } } | ".\target\release\CorvusEcma262Worker.exe"


