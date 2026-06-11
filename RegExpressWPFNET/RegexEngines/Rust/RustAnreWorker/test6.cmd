rem set RUST_BACKTRACE=1
echo { "pattern" : "(a)|(b)", "text" : "b" } | ".\target\release\RustAnreWorker.exe"
echo { "pattern" : "(?<nameA>a)|(?<nameB>b)", "text" : "b" } | ".\target\release\RustAnreWorker.exe"
