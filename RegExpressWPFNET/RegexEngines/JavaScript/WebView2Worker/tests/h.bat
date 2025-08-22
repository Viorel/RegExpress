@rem Helper used by other tests
@echo Params: %1 %2 %3 %4
@..\bin\Debug\x64\WebView2Worker.exe %1 %2 %3 %4 1>o 2>e & type o & echo --- & type e

