@echo on

set Configuration=%~1
set Platform=%~2
set TargetFramework=net7.0-windows

set ThisCmdPath=%~dp0

rem echo %ConfigurationName%
rem echo %ThisCmdPath%

set SolutionDir=%ThisCmdPath%\..
set EnginesTargetDir=%SolutionDir%\RegExpressWPFNET\bin\%Configuration%\%TargetFramework%\Engines

rem echo %SolutionDir%
rem echo %TargetDir%


rem -- .NET 7 --

set BaseDir=%SolutionDir%\RegexEngines\DotNET7
xcopy /D /R /Y "%BaseDir%\DotNET7Plugin\bin\%Configuration%\%TargetFramework%\DotNET7Plugin.dll" "%EnginesTargetDir%\DotNET7\*"


rem -- .NET Framework 4.8 --

set BaseDir=%SolutionDir%\RegexEngines\DotNETFramework4_8
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkPlugin\bin\%Configuration%\%TargetFramework%\DotNETFrameworkPlugin.dll" "%EnginesTargetDir%\DotNETFramework4_8\*"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkClient\bin\%Configuration%\DotNETFrameworkClient.exe" "%EnginesTargetDir%\DotNETFramework4_8\Client\*.bin"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkClient\bin\%Configuration%\DotNETFrameworkClient.exe.config" "%EnginesTargetDir%\DotNETFramework4_8\Client\*"
ren "%EnginesTargetDir%\DotNETFramework4_8\Client\DotNETFrameworkClient.exe.config" "DotNETFrameworkClient.bin.config"
xcopy /D /R /Y "%BaseDir%\DotNETFrameworkClient\bin\%Configuration%\*.dll" "%EnginesTargetDir%\DotNETFramework4_8\Client\*"
rem xcopy /D /R /Y "%BaseDir%\DotNETFrameworkClient\bin\%Configuration%\*.config" "%EnginesTargetDir%\DotNETFramework4_8\*.bin"


rem -- STD --

set BaseDir=%SolutionDir%\RegexEngines\Std
xcopy /D /R /Y "%BaseDir%\StdPlugin\bin\%Configuration%\%TargetFramework%\StdPlugin.dll" "%EnginesTargetDir%\Std\*"
xcopy /D /R /Y "%BaseDir%\StdClient\bin\%Configuration%\%Platform%\StdClient.exe" "%EnginesTargetDir%\Std\*.bin"


rem -- RE2 --

set BaseDir=%SolutionDir%\RegexEngines\RE2
xcopy /D /R /Y "%BaseDir%\RE2Plugin\bin\%Configuration%\%TargetFramework%\RE2Plugin.dll" "%EnginesTargetDir%\RE2\*"
xcopy /D /R /Y "%BaseDir%\RE2Client\bin\%Configuration%\%Platform%\RE2Client.exe" "%EnginesTargetDir%\RE2\*.bin"


