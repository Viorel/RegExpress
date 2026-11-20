echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad", "func" : "exec", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
echo { "pattern" : "(a)(b?)(c)?(d)", "text" : "ad", "func" : "matchAll", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
echo { "pattern" : "(?<na>a)(?<nb>b?)(?<nc>c)?(?<nd>d)", "text" : "ad", "func" : "matchAll", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"

