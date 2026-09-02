echo { "pattern" : "(a)(x)?", "text" : "ab" } | ".\target\release\ReXileWorker.exe"
echo { "pattern" : "(?<n1>b)|(?<n2>c)", "text" : "abc" } | ".\target\release\ReXileWorker.exe"
