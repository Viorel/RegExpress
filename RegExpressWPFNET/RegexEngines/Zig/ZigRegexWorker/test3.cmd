echo BAD | zig-out\bin\ZigRegexWorker.exe
echo { "pattern": "(BAD", "text": "abc", "flags": { } } | zig-out\bin\ZigRegexWorker.exe