@echo off
call node --version
echo { "cmd": "v" } | node NodeJsWorker.js
echo { "cmd": "v" } | NodeJsWorker.exe
