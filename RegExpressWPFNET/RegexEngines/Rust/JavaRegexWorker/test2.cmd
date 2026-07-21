@echo { "pattern" : "(.)", "text" : "abc", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"
@echo { "pattern" : "(?<n>.)", "text" : "abc", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"

