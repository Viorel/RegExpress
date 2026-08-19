@echo { "pattern" : "(?<n1>(.))", "text" : "a" } | .\JRE-min\bin\java.exe -cp .;json-simple-1.1.1.jar;joni-2.2.7.jar;jcodings-1.0.64.jar JoniWorker
@echo { "pattern" : "(.)(?<n>.)(.)", "text" : "abc", "options" : { "CAPTURE_GROUP" : true }  } | .\JRE-min\bin\java.exe -cp .;json-simple-1.1.1.jar;joni-2.2.7.jar;jcodings-1.0.64.jar JoniWorker
