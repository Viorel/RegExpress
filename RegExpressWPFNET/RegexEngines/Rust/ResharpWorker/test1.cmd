@echo { "pattern" : ".", "text" : "a", "options" : { "implicit_captures" : false } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : ".", "text" : "a", "options" : { "implicit_captures" : true } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : "(.)", "text" : "a", "options" : { "implicit_captures" : false } } | "target\release\RustResharpWorker.exe"
@echo { "pattern" : "(.)", "text" : "a", "options" : { "implicit_captures" : true } } | "target\release\RustResharpWorker.exe"
