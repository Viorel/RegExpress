@echo on

set Configuration=%~1
set Platform=%~2
set TargetDir=net9.0-windows7.0

set ThisCmdPath=%~dp0

rem echo %ConfigurationName%
rem echo %ThisCmdPath%

set SolutionDir=%ThisCmdPath%..
set EnginesTargetPath=%SolutionDir%\RegExpressWPFNET\bin\%Configuration%\%TargetDir%\Engines

rem echo %SolutionDir%
rem echo %TargetDir%


rem -- .NET 9 --

set BasePath=%SolutionDir%\RegexEngines\DotNET9
xcopy /D /R /Y "%BasePath%\DotNET9Plugin\bin\%Configuration%\%TargetDir%\DotNET9Plugin.dll" "%EnginesTargetPath%\DotNET9\*"
xcopy /D /R /Y "%BasePath%\DotNET9Worker\bin\%Configuration%\net9.0-windows7.0\DotNET9Worker.dll" "%EnginesTargetPath%\DotNET9\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET9Worker\bin\%Configuration%\net9.0-windows7.0\DotNET9Worker.exe" "%EnginesTargetPath%\DotNET9\Worker\*.bin"
xcopy /D /R /Y "%BasePath%\DotNET9Worker\bin\%Configuration%\net9.0-windows7.0\DotNET9Worker.deps.json" "%EnginesTargetPath%\DotNET9\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET9Worker\bin\%Configuration%\net9.0-windows7.0\DotNET9Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET9\Worker\*"


rem -- .NET 8 --

set BasePath=%SolutionDir%\RegexEngines\DotNET8
xcopy /D /R /Y "%BasePath%\DotNET8Plugin\bin\%Configuration%\%TargetDir%\DotNET8Plugin.dll" "%EnginesTargetPath%\DotNET8\*"
xcopy /D /R /Y "%BasePath%\DotNET8Worker\bin\%Configuration%\net8.0-windows\DotNET8Worker.dll" "%EnginesTargetPath%\DotNET8\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET8Worker\bin\%Configuration%\net8.0-windows\DotNET8Worker.exe" "%EnginesTargetPath%\DotNET8\Worker\*.bin"
xcopy /D /R /Y "%BasePath%\DotNET8Worker\bin\%Configuration%\net8.0-windows\DotNET8Worker.deps.json" "%EnginesTargetPath%\DotNET8\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET8Worker\bin\%Configuration%\net8.0-windows\DotNET8Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET8\Worker\*"


rem -- .NET 7 --

set BasePath=%SolutionDir%\RegexEngines\DotNET7
xcopy /D /R /Y "%BasePath%\DotNET7Plugin\bin\%Configuration%\%TargetDir%\DotNET7Plugin.dll" "%EnginesTargetPath%\DotNET7\*"
xcopy /D /R /Y "%BasePath%\DotNET7Worker\bin\%Configuration%\net7.0-windows7.0\DotNET7Worker.dll" "%EnginesTargetPath%\DotNET7\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET7Worker\bin\%Configuration%\net7.0-windows7.0\DotNET7Worker.exe" "%EnginesTargetPath%\DotNET7\Worker\*.bin"
xcopy /D /R /Y "%BasePath%\DotNET7Worker\bin\%Configuration%\net7.0-windows7.0\DotNET7Worker.deps.json" "%EnginesTargetPath%\DotNET7\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET7Worker\bin\%Configuration%\net7.0-windows7.0\DotNET7Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET7\Worker\*"


rem -- .NET 6 --

set BasePath=%SolutionDir%\RegexEngines\DotNET6
xcopy /D /R /Y "%BasePath%\DotNET6Plugin\bin\%Configuration%\%TargetDir%\DotNET6Plugin.dll" "%EnginesTargetPath%\DotNET6\*"
xcopy /D /R /Y "%BasePath%\DotNET6Worker\bin\%Configuration%\net6.0-windows\DotNET6Worker.dll" "%EnginesTargetPath%\DotNET6\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET6Worker\bin\%Configuration%\net6.0-windows\DotNET6Worker.exe" "%EnginesTargetPath%\DotNET6\Worker\*.bin"
xcopy /D /R /Y "%BasePath%\DotNET6Worker\bin\%Configuration%\net6.0-windows\DotNET6Worker.deps.json" "%EnginesTargetPath%\DotNET6\Worker\*"
xcopy /D /R /Y "%BasePath%\DotNET6Worker\bin\%Configuration%\net6.0-windows\DotNET6Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET6\Worker\*"


rem -- .NET Framework 4.8 --

set BasePath=%SolutionDir%\RegexEngines\DotNETFramework4_8
xcopy /D /R /Y "%BasePath%\DotNETFrameworkPlugin\bin\%Configuration%\%TargetDir%\DotNETFrameworkPlugin.dll" "%EnginesTargetPath%\DotNETFramework4_8\*"
xcopy /D /R /Y "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe" "%EnginesTargetPath%\DotNETFramework4_8\Worker\*.bin"
xcopy /D /R /Y "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe.config" "%EnginesTargetPath%\DotNETFramework4_8\Worker\*"
copy /Y "%EnginesTargetPath%\DotNETFramework4_8\Worker\DotNETFrameworkWorker.exe.config" "%EnginesTargetPath%\DotNETFramework4_8\Worker\DotNETFrameworkWorker.bin.config" > nul
xcopy /D /R /Y "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\*.dll" "%EnginesTargetPath%\DotNETFramework4_8\Worker\*"
rem xcopy /D /R /Y "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\*.config" "%EnginesTargetPath%\DotNETFramework4_8\*.bin"


rem -- STD --

set BasePath=%SolutionDir%\RegexEngines\Std
xcopy /D /R /Y "%BasePath%\StdPlugin\bin\%Configuration%\%TargetDir%\StdPlugin.dll" "%EnginesTargetPath%\Std\*"
xcopy /D /R /Y "%BasePath%\StdWorker\bin\%Configuration%\%Platform%\StdWorker.exe" "%EnginesTargetPath%\Std\*.bin"


rem -- RE2 --

set BasePath=%SolutionDir%\RegexEngines\RE2
xcopy /D /R /Y "%BasePath%\RE2Plugin\bin\%Configuration%\%TargetDir%\RE2Plugin.dll" "%EnginesTargetPath%\RE2\*"
xcopy /D /R /Y "%BasePath%\RE2Worker\bin\%Configuration%\%Platform%\RE2Worker.exe" "%EnginesTargetPath%\RE2\*.bin"


rem -- SubReg --

set BasePath=%SolutionDir%\RegexEngines\SubReg
xcopy /D /R /Y "%BasePath%\SubRegPlugin\bin\%Configuration%\%TargetDir%\SubRegPlugin.dll" "%EnginesTargetPath%\SubReg\*"
xcopy /D /R /Y "%BasePath%\SubRegWorker\bin\%Configuration%\%Platform%\SubRegWorker.exe" "%EnginesTargetPath%\SubReg\*.bin"


rem -- PCRE2 --

set BasePath=%SolutionDir%\RegexEngines\PCRE2
xcopy /D /R /Y "%BasePath%\PCRE2Plugin\bin\%Configuration%\%TargetDir%\PCRE2Plugin.dll" "%EnginesTargetPath%\PCRE2\*"
xcopy /D /R /Y "%BasePath%\PCRE2Worker\bin\%Configuration%\%Platform%\PCRE2Worker.exe" "%EnginesTargetPath%\PCRE2\*.bin"


rem -- Boost --

set BasePath=%SolutionDir%\RegexEngines\Boost
xcopy /D /R /Y "%BasePath%\BoostPlugin\bin\%Configuration%\%TargetDir%\BoostPlugin.dll" "%EnginesTargetPath%\Boost\*"
xcopy /D /R /Y "%BasePath%\BoostWorker\bin\%Configuration%\%Platform%\BoostWorker.exe" "%EnginesTargetPath%\Boost\*.bin"


rem -- Oniguruma --

set BasePath=%SolutionDir%\RegexEngines\Oniguruma
xcopy /D /R /Y "%BasePath%\OnigurumaPlugin\bin\%Configuration%\%TargetDir%\OnigurumaPlugin.dll" "%EnginesTargetPath%\Oniguruma\*"
xcopy /D /R /Y "%BasePath%\OnigurumaWorker\bin\%Configuration%\%Platform%\OnigurumaWorker.exe" "%EnginesTargetPath%\Oniguruma\*.bin"


rem -- WebView2 --

set BasePath=%SolutionDir%\RegexEngines\WebView2
xcopy /D /R /Y "%BasePath%\WebView2Plugin\bin\%Configuration%\%TargetDir%\WebView2Plugin.dll" "%EnginesTargetPath%\WebView2\*"
xcopy /D /R /Y "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Worker.exe" "%EnginesTargetPath%\WebView2\*.bin"
xcopy /D /R /Y "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Loader.dll" "%EnginesTargetPath%\WebView2\*"


rem -- VBScript --

set BasePath=%SolutionDir%\RegexEngines\VBScript
xcopy /D /R /Y "%BasePath%\VBScriptPlugin\bin\%Configuration%\%TargetDir%\VBScriptPlugin.dll" "%EnginesTargetPath%\VBScript\*"
xcopy /D /R /Y "%BasePath%\VBScriptWorker\VBScriptWorker.vbs" "%EnginesTargetPath%\VBScript\*"
