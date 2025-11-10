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
xcopy /D /R /Y "%BasePath%\GccWorker\GccWorker.exe" "%EnginesTargetPath%\Std\*.bin"
xcopy /D /R /Y "%BasePath%\SrellWorker\bin\%Configuration%\%Platform%\SrellWorker.exe" "%EnginesTargetPath%\Std\*.bin"


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


rem -- JavaScript --

set BasePath=%SolutionDir%\RegexEngines\JavaScript
xcopy /D /R /Y "%BasePath%\JavaScriptPlugin\bin\%Configuration%\%TargetDir%\JavaScriptPlugin.dll" "%EnginesTargetPath%\JavaScript\*"

xcopy /D /R /Y "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Worker.exe" "%EnginesTargetPath%\JavaScript\*.bin"
xcopy /D /R /Y "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Loader.dll" "%EnginesTargetPath%\JavaScript\*"

xcopy /D /R /Y "%BasePath%\NodeJsWorker\NodeJsWorker.zip" "%EnginesTargetPath%\JavaScript\*.zip"

xcopy /D /R /Y /E "%BasePath%\QuickJsWorker\QuickJs\*.*" "%EnginesTargetPath%\JavaScript\QuickJsWorker\*"
xcopy /D /R /Y "%BasePath%\QuickJsWorker\QuickJsWorker.js" "%EnginesTargetPath%\JavaScript\QuickJsWorker\*"

xcopy /D /R /Y /E "%BasePath%\QuickJsWorker\RE2JSWorker\re2js\*.*" "%EnginesTargetPath%\JavaScript\RE2JSWorker\re2js\*"
xcopy /D /R /Y "%BasePath%\QuickJsWorker\RE2JSWorker\RE2JSWorker.js" "%EnginesTargetPath%\JavaScript\RE2JSWorker\*"

xcopy /D /R /Y "%BasePath%\SpiderMonkeyWorker\SpiderMonkey.zip" "%EnginesTargetPath%\JavaScript\SpiderMonkeyWorker\*.zip"
xcopy /D /R /Y "%BasePath%\SpiderMonkeyWorker\SpiderMonkeyWorker.js" "%EnginesTargetPath%\JavaScript\SpiderMonkeyWorker\*"

xcopy /D /R /Y "%BasePath%\BunWorker\bun-windows-x64-baseline.zip" "%EnginesTargetPath%\JavaScript\BunWorker\*.zip"
xcopy /D /R /Y "%BasePath%\BunWorker\BunWorker.js" "%EnginesTargetPath%\JavaScript\BunWorker\*"


rem -- VBScript --

set BasePath=%SolutionDir%\RegexEngines\VBScript
xcopy /D /R /Y "%BasePath%\VBScriptPlugin\bin\%Configuration%\%TargetDir%\VBScriptPlugin.dll" "%EnginesTargetPath%\VBScript\*"
xcopy /D /R /Y "%BasePath%\VBScriptWorker\VBScriptWorker.vbs" "%EnginesTargetPath%\VBScript\*"


rem -- Hyperscan --

set BasePath=%SolutionDir%\RegexEngines\Hyperscan
xcopy /D /R /Y "%BasePath%\HyperscanPlugin\bin\%Configuration%\%TargetDir%\HyperscanPlugin.dll" "%EnginesTargetPath%\Hyperscan\*"
xcopy /D /R /Y "%BasePath%\HyperscanWorker\bin\%Configuration%\%Platform%\HyperscanWorker.exe" "%EnginesTargetPath%\Hyperscan\*.bin"


rem -- ICU --

set BasePath=%SolutionDir%\RegexEngines\ICU
xcopy /D /R /Y "%BasePath%\ICUPlugin\bin\%Configuration%\%TargetDir%\ICUPlugin.dll" "%EnginesTargetPath%\ICU\*"
xcopy /D /R /Y "%BasePath%\ICUWorker\bin\%Configuration%\%Platform%\ICUWorker.exe" "%EnginesTargetPath%\ICU\*.bin"
xcopy /D /R /Y "%BasePath%\ICUWorker\ICU-min\bin64\*" "%EnginesTargetPath%\ICU\*"


rem -- Rust --

set BasePath=%SolutionDir%\RegexEngines\Rust
xcopy /D /R /Y "%BasePath%\RustPlugin\bin\%Configuration%\%TargetDir%\RustPlugin.dll" "%EnginesTargetPath%\Rust\*"
xcopy /D /R /Y "%BasePath%\RustRegexWorker\target\release\RustRegexWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
xcopy /D /R /Y "%BasePath%\RustFancyWorker\target\release\RustFancyWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
xcopy /D /R /Y "%BasePath%\RustRegressWorker\target\release\RustRegressWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
xcopy /D /R /Y "%BasePath%\RustRegexLiteWorker\target\release\RustRegexLiteWorker.exe" "%EnginesTargetPath%\Rust\*.bin"


rem -- Java --

set BasePath=%SolutionDir%\RegexEngines\Java
xcopy /D /R /Y "%BasePath%\JavaPlugin\bin\%Configuration%\%TargetDir%\JavaPlugin.dll" "%EnginesTargetPath%\Java\*"
xcopy /D /R /Y "%BasePath%\JavaWorker\JavaWorker.class" "%EnginesTargetPath%\Java\*"
xcopy /D /R /Y "%BasePath%\JavaWorker\JRE-min.zip" "%EnginesTargetPath%\Java\*"
xcopy /D /R /Y "%BasePath%\JavaWorker\RE2JWorker.class" "%EnginesTargetPath%\Java\*"
xcopy /D /R /Y "%BasePath%\JavaWorker\re2j-1.8.jar" "%EnginesTargetPath%\Java\*"


rem -- Python --

set BasePath=%SolutionDir%\RegexEngines\Python
xcopy /D /R /Y "%BasePath%\PythonPlugin\bin\%Configuration%\%TargetDir%\PythonPlugin.dll" "%EnginesTargetPath%\Python\*"
xcopy /D /R /Y /E "%BasePath%\PythonWorker\python-embed-amd64\*" "%EnginesTargetPath%\Python\python-embed-amd64\*"
xcopy /D /R /Y "%BasePath%\PythonWorker\PythonWorker.py" "%EnginesTargetPath%\Python\*"


rem -- D --

set BasePath=%SolutionDir%\RegexEngines\D
xcopy /D /R /Y "%BasePath%\DPlugin\bin\%Configuration%\%TargetDir%\DPlugin.dll" "%EnginesTargetPath%\D\*"
xcopy /D /R /Y "%BasePath%\DWorker\DWorker.exe" "%EnginesTargetPath%\D\*.bin"


rem -- Perl --

set BasePath=%SolutionDir%\RegexEngines\Perl
xcopy /D /R /Y "%BasePath%\PerlPlugin\bin\%Configuration%\%TargetDir%\PerlPlugin.dll" "%EnginesTargetPath%\Perl\*"
xcopy /D /R /Y "%BasePath%\PerlWorker\PerlWorker.pl" "%EnginesTargetPath%\Perl\*"
xcopy /D /R /Y /E "%BasePath%\PerlWorker\Perl-min\*" "%EnginesTargetPath%\Perl\Perl-min\*"


rem -- Fortran --

set BasePath=%SolutionDir%\RegexEngines\Fortran
xcopy /D /R /Y "%BasePath%\FortranPlugin\bin\%Configuration%\%TargetDir%\FortranPlugin.dll" "%EnginesTargetPath%\Fortran\*"
xcopy /D /R /Y "%BasePath%\FortranForgexWorker\x64\Release\FortranForgexWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"
xcopy /D /R /Y "%BasePath%\FortranRegexJeyemhexWorker\x64\Release\FortranRegexJeyemhexWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"
xcopy /D /R /Y "%BasePath%\FortranRegexPerazzWorker\x64\Release\FortranRegexPerazzWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"


rem -- TRE --

set BasePath=%SolutionDir%\RegexEngines\TRE
xcopy /D /R /Y "%BasePath%\TREPlugin\bin\%Configuration%\%TargetDir%\TREPlugin.dll" "%EnginesTargetPath%\TRE\*"
xcopy /D /R /Y "%BasePath%\TREWorker\bin\%Configuration%\%Platform%\TREWorker.exe" "%EnginesTargetPath%\TRE\*.bin"
xcopy /D /R /Y "%BasePath%\TRE\TRE\win32\bin\%Configuration%\%Platform%\tre.dll" "%EnginesTargetPath%\TRE\*"


rem -- tiny-regex-c --

set BasePath=%SolutionDir%\RegexEngines\TinyRegexC
xcopy /D /R /Y "%BasePath%\TinyRegexCPlugin\bin\%Configuration%\%TargetDir%\TinyRegexCPlugin.dll" "%EnginesTargetPath%\TinyRegexC\*"
xcopy /D /R /Y "%BasePath%\TinyRegexCWorker\bin\%Configuration%\%Platform%\TinyRegexCWorker.exe" "%EnginesTargetPath%\TinyRegexC\*.bin"


rem -- Ada --

set BasePath=%SolutionDir%\RegexEngines\Ada
xcopy /D /R /Y "%BasePath%\AdaPlugin\bin\%Configuration%\%TargetDir%\AdaPlugin.dll" "%EnginesTargetPath%\Ada\*"
xcopy /D /R /Y "%BasePath%\AdaWorker\adaworker.exe" "%EnginesTargetPath%\Ada\*.bin"


rem -- C++Builder --

set BasePath=%SolutionDir%\RegexEngines\CppBuilder
xcopy /D /R /Y "%BasePath%\CppBuilderPlugin\bin\%Configuration%\%TargetDir%\CppBuilderPlugin.dll" "%EnginesTargetPath%\CppBuilder\*"
xcopy /D /R /Y "%BasePath%\CppBuilderWorker\Win64\Release\CppBuilderWorker.exe" "%EnginesTargetPath%\CppBuilder\*.bin"


rem -- Qt --

set BasePath=%SolutionDir%\RegexEngines\Qt
xcopy /D /R /Y "%BasePath%\QtPlugin\bin\%Configuration%\%TargetDir%\QtPlugin.dll" "%EnginesTargetPath%\Qt\*"
xcopy /D /R /Y "%BasePath%\QtWorker\x64\Release\QtWorker.exe" "%EnginesTargetPath%\Qt\*.bin"
xcopy /D /R /Y "%BasePath%\QtWorker\x64\Release\Qt6Core.dll" "%EnginesTargetPath%\Qt\*"


rem -- CTRE --

set BasePath=%SolutionDir%\RegexEngines\CompileTimeRegex
xcopy /D /R /Y "%BasePath%\CompileTimeRegexPlugin\bin\%Configuration%\%TargetDir%\CompileTimeRegexPlugin.dll" "%EnginesTargetPath%\CompileTimeRegex\*"
xcopy /D /R /Y /E "%BasePath%\CompileTimeRegexSample\compile-time-regular-expressions\*" "%EnginesTargetPath%\CompileTimeRegex\compile-time-regular-expressions\*"
xcopy /D /R /Y "%BasePath%\CompileTimeRegexSample\build.cmd" "%EnginesTargetPath%\CompileTimeRegex\*"
xcopy /D /R /Y "%BasePath%\CompileTimeRegexSample\CompileTimeRegexSample.cpp" "%EnginesTargetPath%\CompileTimeRegex\*"

