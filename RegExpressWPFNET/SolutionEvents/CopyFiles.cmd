@echo on

set Configuration=%~1
set Platform=%~2
set TargetFramework=net7.0-windows

set ThisCmdPath=%~dp0

rem echo %ConfigurationName%
rem echo %ThisCmdPath%

set SolutionDir=%ThisCmdPath%..
set EnginesTargetDir=%SolutionDir%\RegExpressWPFNET\bin\%Configuration%\%TargetFramework%\Engines

rem echo %SolutionDir%
rem echo %TargetDir%


rem -- .NET 7 --

set BaseDir=%SolutionDir%\RegexEngines\DotNET7
xcopy /D /R /Y "%BaseDir%\DotNET7Plugin\bin\%Configuration%\%TargetFramework%\DotNET7Plugin.dll" "%EnginesTargetDir%\DotNET7\*"


rem -- .NET 6 --

set BaseDir=%SolutionDir%\RegexEngines\DotNET6
xcopy /D /R /Y "%BaseDir%\DotNET6Plugin\bin\%Configuration%\%TargetFramework%\DotNET6Plugin.dll" "%EnginesTargetDir%\DotNET6\*"
xcopy /D /R /Y "%BaseDir%\DotNET6Worker\bin\%Configuration%\net6.0\DotNET6Worker.dll" "%EnginesTargetDir%\DotNET6\Worker\*"
xcopy /D /R /Y "%BaseDir%\DotNET6Worker\bin\%Configuration%\net6.0\DotNET6Worker.exe" "%EnginesTargetDir%\DotNET6\Worker\*.bin"
xcopy /D /R /Y "%BaseDir%\DotNET6Worker\bin\%Configuration%\net6.0\DotNET6Worker.deps.json" "%EnginesTargetDir%\DotNET6\Worker\*"
xcopy /D /R /Y "%BaseDir%\DotNET6Worker\bin\%Configuration%\net6.0\DotNET6Worker.runtimeconfig.json" "%EnginesTargetDir%\DotNET6\Worker\*"


rem -- .NET Framework 4.8 --

set BaseDir=%SolutionDir%\RegexEngines\DotNETFramework4_8
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkPlugin\bin\%Configuration%\%TargetFramework%\DotNETFrameworkPlugin.dll" "%EnginesTargetDir%\DotNETFramework4_8\*"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe" "%EnginesTargetDir%\DotNETFramework4_8\Worker\*.bin"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe.config" "%EnginesTargetDir%\DotNETFramework4_8\Worker\*"
ren "%EnginesTargetDir%\DotNETFramework4_8\Worker\DotNETFrameworkWorker.exe.config" "DotNETFrameworkWorker.bin.config"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkWorker\bin\%Configuration%\*.dll" "%EnginesTargetDir%\DotNETFramework4_8\Worker\*"
rem xcopy /D /R /Y "%BaseDir%\DotNETFrameworkWorker\bin\%Configuration%\*.config" "%EnginesTargetDir%\DotNETFramework4_8\*.bin"


rem -- STD --

set BaseDir=%SolutionDir%\RegexEngines\Std
xcopy /D /R /Y "%BaseDir%\StdPlugin\bin\%Configuration%\%TargetFramework%\StdPlugin.dll" "%EnginesTargetDir%\Std\*"
xcopy /D /R /Y "%BaseDir%\StdWorker\bin\%Configuration%\%Platform%\StdWorker.exe" "%EnginesTargetDir%\Std\*.bin"


rem -- RE2 --

set BaseDir=%SolutionDir%\RegexEngines\RE2
xcopy /D /R /Y "%BaseDir%\RE2Plugin\bin\%Configuration%\%TargetFramework%\RE2Plugin.dll" "%EnginesTargetDir%\RE2\*"
xcopy /D /R /Y "%BaseDir%\RE2Worker\bin\%Configuration%\%Platform%\RE2Worker.exe" "%EnginesTargetDir%\RE2\*.bin"


rem -- SubReg --

set BaseDir=%SolutionDir%\RegexEngines\SubReg
xcopy /D /R /Y "%BaseDir%\SubRegPlugin\bin\%Configuration%\%TargetFramework%\SubRegPlugin.dll" "%EnginesTargetDir%\SubReg\*"
xcopy /D /R /Y "%BaseDir%\SubRegWorker\bin\%Configuration%\%Platform%\SubRegWorker.exe" "%EnginesTargetDir%\SubReg\*.bin"


rem -- PCRE2 --

set BaseDir=%SolutionDir%\RegexEngines\PCRE2
xcopy /D /R /Y "%BaseDir%\PCRE2Plugin\bin\%Configuration%\%TargetFramework%\PCRE2Plugin.dll" "%EnginesTargetDir%\PCRE2\*"
xcopy /D /R /Y "%BaseDir%\PCRE2Worker\bin\%Configuration%\%Platform%\PCRE2Worker.exe" "%EnginesTargetDir%\PCRE2\*.bin"


rem -- Boost --

set BaseDir=%SolutionDir%\RegexEngines\Boost
xcopy /D /R /Y "%BaseDir%\BoostPlugin\bin\%Configuration%\%TargetFramework%\BoostPlugin.dll" "%EnginesTargetDir%\Boost\*"
xcopy /D /R /Y "%BaseDir%\BoostWorker\bin\%Configuration%\%Platform%\BoostWorker.exe" "%EnginesTargetDir%\Boost\*.bin"


rem -- Oniguruma --

set BaseDir=%SolutionDir%\RegexEngines\Oniguruma
xcopy /D /R /Y "%BaseDir%\OnigurumaPlugin\bin\%Configuration%\%TargetFramework%\OnigurumaPlugin.dll" "%EnginesTargetDir%\Oniguruma\*"
xcopy /D /R /Y "%BaseDir%\OnigurumaWorker\bin\%Configuration%\%Platform%\OnigurumaWorker.exe" "%EnginesTargetDir%\Oniguruma\*.bin"


rem -- WebView2 --

set BaseDir=%SolutionDir%\RegexEngines\WebView2
xcopy /D /R /Y "%BaseDir%\WebView2Plugin\bin\%Configuration%\%TargetFramework%\WebView2Plugin.dll" "%EnginesTargetDir%\WebView2\*"
xcopy /D /R /Y "%BaseDir%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Worker.exe" "%EnginesTargetDir%\WebView2\*.bin"
xcopy /D /R /Y "%BaseDir%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Loader.dll" "%EnginesTargetDir%\WebView2\*"
