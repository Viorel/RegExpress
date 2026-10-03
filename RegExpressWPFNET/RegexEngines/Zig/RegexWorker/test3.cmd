echo BAD | zig-out\bin\RegexWorker.exe
echo { "pattern": "(BAD", "text": "abc", "flags": { } } | zig-out\bin\RegexWorker.exe