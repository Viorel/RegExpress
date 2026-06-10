echo BAD | "zig-out\bin\ZigPzreWorker.exe"
echo { "pattern": ".", "flags": {} } | "zig-out\bin\ZigPzreWorker.exe"
echo { "pattern": ".", "text": "abc", "UNKNOWN": 0, "flags": {} } | "zig-out\bin\ZigPzreWorker.exe"
echo { "pattern": "(BAD", "text": "abc", "flags": {} } | "zig-out\bin\ZigPzreWorker.exe"