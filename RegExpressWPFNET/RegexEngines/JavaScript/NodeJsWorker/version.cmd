@echo off
call node --version
echo { "cmd": "version" } | node NodeJsWorker.js
echo { "cmd": "version" } | NodeJsWorker.exe
