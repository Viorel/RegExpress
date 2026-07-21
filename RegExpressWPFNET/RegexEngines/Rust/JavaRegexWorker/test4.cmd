@echo { "pattern" : "BAD (.", "text" : "abc", "flags" : "" } | ".\target\release\RustJavaRegexWorker.exe"
@echo { "pattern" : ".", "text" : "abc", "flags" : "BAD FLAGS" } | ".\target\release\RustJavaRegexWorker.exe"

