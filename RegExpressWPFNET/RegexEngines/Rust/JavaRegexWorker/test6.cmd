@echo { "pattern" : "(a*)*b", "text" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac", "flags" : "uU" } | ".\target\release\RustJavaRegexWorker.exe"
@echo { "pattern" : "(a+)+b", "text" : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac", "flags" : "uU" } | ".\target\release\RustJavaRegexWorker.exe"

