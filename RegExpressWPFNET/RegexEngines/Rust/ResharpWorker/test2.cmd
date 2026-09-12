echo { "pattern" : "..", "text" : "abcd", "options" : { } } | "target\release\RustResharpWorker.exe"
echo { "pattern" : "(.)(.)", "text" : "abcd", "options" : { } } | "target\release\RustResharpWorker.exe"
echo { "pattern" : "(.)(?<n1>.)(.)(?<n2>.)", "text" : "abcd", "options" : { } } | "target\release\RustResharpWorker.exe"
