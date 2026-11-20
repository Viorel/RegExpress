echo { "pattern" : ".", "text" : "abc", "func" : "matchAll", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
echo { "pattern" : "z", "text" : "abc", "func" : "matchAll", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
echo { "pattern" : "(.)", "text" : "abc", "func" : "matchAll", "flags" : "g" } | "bin\Debug\net10.0\JintWorker.exe"
