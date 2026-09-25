@echo { "pattern" : ".", "text" : "abc", "options" : { "debug" : true } } | .\JRE-min\bin\java.exe -cp .;reggie-0.4.0.jar;asm-9.9.1.jar;asm-commons-9.9.1.jar;asm-util-9.9.1.jar;json-simple-1.1.1.jar ReggieWorker
@echo { "pattern" : "(?<n>.)", "text" : "abc", "options" : { "debug" : true } } | .\JRE-min\bin\java.exe -cp .;reggie-0.4.0.jar;asm-9.9.1.jar;asm-commons-9.9.1.jar;asm-util-9.9.1.jar;json-simple-1.1.1.jar ReggieWorker

@echo { "pattern" : "(?<=a.+)b", "text" : "aXXb", "options" : { "debug" : true, "ALLOW_JDK_FALLBACK" : true } } | .\JRE-min\bin\java.exe -cp .;reggie-0.4.0.jar;asm-9.9.1.jar;asm-commons-9.9.1.jar;asm-util-9.9.1.jar;json-simple-1.1.1.jar ReggieWorker

