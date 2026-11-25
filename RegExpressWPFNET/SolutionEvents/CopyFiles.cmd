@echo on

set Configuration=%~1
set Platform=%~2
set TargetDir=net10.0-windows7.0

set ThisCmdPath=%~dp0

rem echo %ConfigurationName%
rem echo %ThisCmdPath%

set SolutionDir=%ThisCmdPath%..
set EnginesTargetPath=%SolutionDir%\RegExpressWPFNET\bin\%Configuration%\%TargetDir%\Engines

rem echo %SolutionDir%
rem echo %TargetDir%


rem -- .NET --

set BasePath=%SolutionDir%\RegexEngines\DotNET
call :cpy "%BasePath%\DotNETPlugin\bin\%Configuration%\%TargetDir%\DotNETPlugin.dll" "%EnginesTargetPath%\DotNET\*"

call :cpy "%BasePath%\DotNET10Worker\bin\%Configuration%\net10.0-windows7.0\DotNET10Worker.exe" "%EnginesTargetPath%\DotNET\DotNET10Worker\*.bin"
call :cpy "%BasePath%\DotNET10Worker\bin\%Configuration%\net10.0-windows7.0\DotNET10Worker.dll" "%EnginesTargetPath%\DotNET\DotNET10Worker\*"
call :cpy "%BasePath%\DotNET10Worker\bin\%Configuration%\net10.0-windows7.0\DotNET10Worker.deps.json" "%EnginesTargetPath%\DotNET\DotNET10Worker\*"
call :cpy "%BasePath%\DotNET10Worker\bin\%Configuration%\net10.0-windows7.0\DotNET10Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\DotNET10Worker\*"

call :cpy "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe" "%EnginesTargetPath%\DotNET\FrameworkWorker\*.bin"
call :cpy "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\DotNETFrameworkWorker.exe.config" "%EnginesTargetPath%\DotNET\FrameworkWorker\*"
copy /Y "%EnginesTargetPath%\DotNET\FrameworkWorker\DotNETFrameworkWorker.exe.config" "%EnginesTargetPath%\DotNET\FrameworkWorker\DotNETFrameworkWorker.bin.config" > nul
call :cpy "%BasePath%\DotNETFrameworkWorker\bin\%Configuration%\*.dll" "%EnginesTargetPath%\DotNET\FrameworkWorker\*"

call :cpy /E "%BasePath%\RESharpWorker\bin\%Configuration%\net10.0-windows7.0\*" "%EnginesTargetPath%\DotNET\RESharpWorker\*"

call :cpy "%BasePath%\ScoutWorker\bin\%Configuration%\net10.0\ScoutWorker.exe" "%EnginesTargetPath%\DotNET\ScoutWorker\*.bin"
call :cpy "%BasePath%\ScoutWorker\bin\%Configuration%\net10.0\ScoutWorker.dll" "%EnginesTargetPath%\DotNET\ScoutWorker\*"
call :cpy "%BasePath%\ScoutWorker\bin\%Configuration%\net10.0\ScoutWorker.deps.json" "%EnginesTargetPath%\DotNET\ScoutWorker\*"
call :cpy "%BasePath%\ScoutWorker\bin\%Configuration%\net10.0\ScoutWorker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\ScoutWorker\*"
call :cpy "%BasePath%\ScoutWorker\bin\%Configuration%\net10.0\Scout.*.dll" "%EnginesTargetPath%\DotNET\ScoutWorker\*"

call :cpy "%BasePath%\LokadUtf8RegexWorker\bin\%Configuration%\net10.0\LokadUtf8RegexWorker.exe" "%EnginesTargetPath%\DotNET\LokadUtf8RegexWorker\*.bin"
call :cpy "%BasePath%\LokadUtf8RegexWorker\bin\%Configuration%\net10.0\LokadUtf8RegexWorker.dll" "%EnginesTargetPath%\DotNET\LokadUtf8RegexWorker\*"
call :cpy "%BasePath%\LokadUtf8RegexWorker\bin\%Configuration%\net10.0\LokadUtf8RegexWorker.deps.json" "%EnginesTargetPath%\DotNET\LokadUtf8RegexWorker\*"
call :cpy "%BasePath%\LokadUtf8RegexWorker\bin\%Configuration%\net10.0\LokadUtf8RegexWorker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\LokadUtf8RegexWorker\*"
call :cpy "%BasePath%\LokadUtf8RegexWorker\bin\%Configuration%\net10.0\Lokad.Utf8Regex.dll" "%EnginesTargetPath%\DotNET\LokadUtf8RegexWorker\*"

call :cpy "%BasePath%\GregsExWorker\bin\%Configuration%\net10.0\*.dll" "%EnginesTargetPath%\DotNET\GregsExWorker\*"
call :cpy "%BasePath%\GregsExWorker\bin\%Configuration%\net10.0\GregsExWorker.exe" "%EnginesTargetPath%\DotNET\GregsExWorker\*.bin"
call :cpy "%BasePath%\GregsExWorker\bin\%Configuration%\net10.0\GregsExWorker.deps.json" "%EnginesTargetPath%\DotNET\GregsExWorker\*"
call :cpy "%BasePath%\GregsExWorker\bin\%Configuration%\net10.0\GregsExWorker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\GregsExWorker\*"

call :cpy "%BasePath%\CorvusEcma262Worker\bin\%Configuration%\net10.0\*.dll" "%EnginesTargetPath%\DotNET\CorvusEcma262Worker\*"
call :cpy "%BasePath%\CorvusEcma262Worker\bin\%Configuration%\net10.0\CorvusEcma262Worker.exe" "%EnginesTargetPath%\DotNET\CorvusEcma262Worker\*.bin"
call :cpy "%BasePath%\CorvusEcma262Worker\bin\%Configuration%\net10.0\CorvusEcma262Worker.deps.json" "%EnginesTargetPath%\DotNET\CorvusEcma262Worker\*"
call :cpy "%BasePath%\CorvusEcma262Worker\bin\%Configuration%\net10.0\CorvusEcma262Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\CorvusEcma262Worker\*"

call :cpy "%BasePath%\FuzzyRegexWorker\bin\%Configuration%\net10.0\*.dll" "%EnginesTargetPath%\DotNET\FuzzyRegexWorker\*"
call :cpy "%BasePath%\FuzzyRegexWorker\bin\%Configuration%\net10.0\FuzzyRegexWorker.exe" "%EnginesTargetPath%\DotNET\FuzzyRegexWorker\*.bin"
call :cpy "%BasePath%\FuzzyRegexWorker\bin\%Configuration%\net10.0\FuzzyRegexWorker.deps.json" "%EnginesTargetPath%\DotNET\FuzzyRegexWorker\*"
call :cpy "%BasePath%\FuzzyRegexWorker\bin\%Configuration%\net10.0\FuzzyRegexWorker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\FuzzyRegexWorker\*"

call :cpy "%BasePath%\DotNET11Worker\bin\Release\net11.0\DotNET11Worker.exe" "%EnginesTargetPath%\DotNET\DotNET11Worker\*.bin"
call :cpy "%BasePath%\DotNET11Worker\bin\Release\net11.0\DotNET11Worker.dll" "%EnginesTargetPath%\DotNET\DotNET11Worker\*"
call :cpy "%BasePath%\DotNET11Worker\bin\Release\net11.0\DotNET11Worker.deps.json" "%EnginesTargetPath%\DotNET\DotNET11Worker\*"
call :cpy "%BasePath%\DotNET11Worker\bin\Release\net11.0\DotNET11Worker.runtimeconfig.json" "%EnginesTargetPath%\DotNET\DotNET11Worker\*"


rem -- STD --

set BasePath=%SolutionDir%\RegexEngines\Std
call :cpy "%BasePath%\StdPlugin\bin\%Configuration%\%TargetDir%\StdPlugin.dll" "%EnginesTargetPath%\Std\*"
call :cpy "%BasePath%\StdWorker\bin\%Configuration%\%Platform%\StdWorker.exe" "%EnginesTargetPath%\Std\*.bin"
call :cpy "%BasePath%\GccWorker\GccWorker.exe" "%EnginesTargetPath%\Std\*.bin"
call :cpy "%BasePath%\SrellWorker\bin\%Configuration%\%Platform%\SrellWorker.exe" "%EnginesTargetPath%\Std\*.bin"
call :cpy "%BasePath%\SrellLinearWorker\bin\%Configuration%\%Platform%\SrellLinearWorker.exe" "%EnginesTargetPath%\Std\*.bin"


rem -- RE2 --

set BasePath=%SolutionDir%\RegexEngines\RE2
call :cpy "%BasePath%\RE2Plugin\bin\%Configuration%\%TargetDir%\RE2Plugin.dll" "%EnginesTargetPath%\RE2\*"
call :cpy "%BasePath%\RE2Worker\bin\%Configuration%\%Platform%\RE2Worker.exe" "%EnginesTargetPath%\RE2\*.bin"


rem -- SubReg --

set BasePath=%SolutionDir%\RegexEngines\SubReg
call :cpy "%BasePath%\SubRegPlugin\bin\%Configuration%\%TargetDir%\SubRegPlugin.dll" "%EnginesTargetPath%\SubReg\*"
call :cpy "%BasePath%\SubRegWorker\bin\%Configuration%\%Platform%\SubRegWorker.exe" "%EnginesTargetPath%\SubReg\*.bin"


rem -- PCRE2 --

set BasePath=%SolutionDir%\RegexEngines\PCRE2
call :cpy "%BasePath%\PCRE2Plugin\bin\%Configuration%\%TargetDir%\PCRE2Plugin.dll" "%EnginesTargetPath%\PCRE2\*"
call :cpy "%BasePath%\PCRE2Worker\bin\%Configuration%\%Platform%\PCRE2Worker.exe" "%EnginesTargetPath%\PCRE2\*.bin"


rem -- Boost --

set BasePath=%SolutionDir%\RegexEngines\Boost
call :cpy "%BasePath%\BoostPlugin\bin\%Configuration%\%TargetDir%\BoostPlugin.dll" "%EnginesTargetPath%\Boost\*"
call :cpy "%BasePath%\BoostWorker\bin\%Configuration%\%Platform%\BoostWorker.exe" "%EnginesTargetPath%\Boost\*.bin"


rem -- Oniguruma --

set BasePath=%SolutionDir%\RegexEngines\Oniguruma
call :cpy "%BasePath%\OnigurumaPlugin\bin\%Configuration%\%TargetDir%\OnigurumaPlugin.dll" "%EnginesTargetPath%\Oniguruma\*"
call :cpy "%BasePath%\OnigurumaWorker\bin\%Configuration%\%Platform%\OnigurumaWorker.exe" "%EnginesTargetPath%\Oniguruma\*.bin"


rem -- JavaScript --

set BasePath=%SolutionDir%\RegexEngines\JavaScript
call :cpy "%BasePath%\JavaScriptPlugin\bin\%Configuration%\%TargetDir%\JavaScriptPlugin.dll" "%EnginesTargetPath%\JavaScript\*"

call :cpy "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Worker.exe" "%EnginesTargetPath%\JavaScript\*.bin"
call :cpy "%BasePath%\WebView2Worker\bin\%Configuration%\%Platform%\WebView2Loader.dll" "%EnginesTargetPath%\JavaScript\*"

call :cpy "%BasePath%\NodeJsWorker\NodeJsWorker.zip" "%EnginesTargetPath%\JavaScript\*.zip"

call :cpy /E "%BasePath%\QuickJsWorker\QuickJs\*.*" "%EnginesTargetPath%\JavaScript\QuickJsWorker\*"
call :cpy "%BasePath%\QuickJsWorker\QuickJsWorker.js" "%EnginesTargetPath%\JavaScript\QuickJsWorker\*"

call :cpy /E "%BasePath%\RE2JSWorker\re2js\*.*" "%EnginesTargetPath%\JavaScript\RE2JSWorker\re2js\*"
call :cpy "%BasePath%\RE2JSWorker\RE2JSWorker.js" "%EnginesTargetPath%\JavaScript\RE2JSWorker\*"

call :cpy "%BasePath%\SpiderMonkeyWorker\SpiderMonkey.zip" "%EnginesTargetPath%\JavaScript\SpiderMonkeyWorker\*.zip"
call :cpy "%BasePath%\SpiderMonkeyWorker\SpiderMonkeyWorker.js" "%EnginesTargetPath%\JavaScript\SpiderMonkeyWorker\*"

call :cpy "%BasePath%\BunWorker\bun-windows-x64-baseline.zip" "%EnginesTargetPath%\JavaScript\BunWorker\*.zip"
call :cpy "%BasePath%\BunWorker\BunWorker.js" "%EnginesTargetPath%\JavaScript\BunWorker\*"

call :cpy /E "%BasePath%\RegexPlusWorker\regex-min\*.*" "%EnginesTargetPath%\JavaScript\RegexPlusWorker\regex-min\*"
call :cpy "%BasePath%\RegexPlusWorker\RegexPlusWorker.js" "%EnginesTargetPath%\JavaScript\RegexPlusWorker\*"

call :cpy "%BasePath%\JintWorker\bin\%Configuration%\net10.0\JintWorker.exe" "%EnginesTargetPath%\JavaScript\Jint\*.bin"
call :cpy "%BasePath%\JintWorker\bin\%Configuration%\net10.0\*.dll" "%EnginesTargetPath%\JavaScript\Jint\*"
call :cpy "%BasePath%\JintWorker\bin\%Configuration%\net10.0\JintWorker.deps.json" "%EnginesTargetPath%\JavaScript\Jint\*"
call :cpy "%BasePath%\JintWorker\bin\%Configuration%\net10.0\JintWorker.runtimeconfig.json" "%EnginesTargetPath%\JavaScript\Jint\*"


rem -- VBScript --

set BasePath=%SolutionDir%\RegexEngines\VBScript
call :cpy "%BasePath%\VBScriptPlugin\bin\%Configuration%\%TargetDir%\VBScriptPlugin.dll" "%EnginesTargetPath%\VBScript\*"
call :cpy "%BasePath%\VBScriptWorker\VBScriptWorker.vbs" "%EnginesTargetPath%\VBScript\*"
call :cpy "%BasePath%\TbRegExpWorker\Build\TbRegExpWorker_win32.exe" "%EnginesTargetPath%\VBScript\*.bin"


rem -- Hyperscan --

set BasePath=%SolutionDir%\RegexEngines\Hyperscan
call :cpy "%BasePath%\HyperscanPlugin\bin\%Configuration%\%TargetDir%\HyperscanPlugin.dll" "%EnginesTargetPath%\Hyperscan\*"
call :cpy "%BasePath%\HyperscanWorker\bin\%Configuration%\%Platform%\HyperscanWorker.exe" "%EnginesTargetPath%\Hyperscan\*.bin"


rem -- ICU --

set BasePath=%SolutionDir%\RegexEngines\ICU
call :cpy "%BasePath%\ICUPlugin\bin\%Configuration%\%TargetDir%\ICUPlugin.dll" "%EnginesTargetPath%\ICU\*"
call :cpy "%BasePath%\ICUWorker\bin\%Configuration%\%Platform%\ICUWorker.exe" "%EnginesTargetPath%\ICU\*.bin"
call :cpy "%BasePath%\ICUWorker\ICU-min\bin64\*" "%EnginesTargetPath%\ICU\*"


rem -- Rust --

set BasePath=%SolutionDir%\RegexEngines\Rust
call :cpy "%BasePath%\RustPlugin\bin\%Configuration%\%TargetDir%\RustPlugin.dll" "%EnginesTargetPath%\Rust\*"
call :cpy "%BasePath%\RegexWorker\target\release\RustRegexWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\FancyWorker\target\release\RustFancyWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\RegressWorker\target\release\RustRegressWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\RegexLiteWorker\target\release\RustRegexLiteWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\ResharpWorker\target\release\RustResharpWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\AnreWorker\target\release\RustAnreWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\RealRegexWorker\target\release\RustRealRegexWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\JavaRegexWorker\target\release\RustJavaRegexWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\RegexrWorker\target\release\RustRegexrWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\ReXileWorker\target\release\ReXileWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\IRegexpRsWorker\target\release\IRegexpRsWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\FerroniWorker\target\release\FerroniWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\RustyExpressionsWorker\target\release\RustyExpressionsWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\DerivreWorker\target\release\DerivreWorker.exe" "%EnginesTargetPath%\Rust\*.bin"
call :cpy "%BasePath%\CorvusEcma262Worker\target\release\CorvusEcma262Worker.exe" "%EnginesTargetPath%\Rust\*.bin"


rem -- Java --

set BasePath=%SolutionDir%\RegexEngines\Java
call :cpy "%BasePath%\JavaPlugin\bin\%Configuration%\%TargetDir%\JavaPlugin.dll" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\JRE-min.zip" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\JavaWorker.class" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\RE2JWorker.class" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\SafeREWorker.class" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\ReggieWorker.class" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\JoniWorker.class" "%EnginesTargetPath%\Java\*"
call :cpy "%BasePath%\JavaWorker\*.jar" "%EnginesTargetPath%\Java\*"


rem -- Python --

set BasePath=%SolutionDir%\RegexEngines\Python
call :cpy "%BasePath%\PythonPlugin\bin\%Configuration%\%TargetDir%\PythonPlugin.dll" "%EnginesTargetPath%\Python\*"
call :cpy /E "%BasePath%\PythonWorker\python-embed-amd64\*" "%EnginesTargetPath%\Python\python-embed-amd64\*"
call :cpy "%BasePath%\PythonWorker\PythonWorkerRe.py" "%EnginesTargetPath%\Python\*"
call :cpy "%BasePath%\PythonWorker\PythonWorkerRegex.py" "%EnginesTargetPath%\Python\*"
call :cpy "%BasePath%\PythonWorker\PythonWorkerRealRegex.py" "%EnginesTargetPath%\Python\*"


rem -- D --

set BasePath=%SolutionDir%\RegexEngines\D
call :cpy "%BasePath%\DPlugin\bin\%Configuration%\%TargetDir%\DPlugin.dll" "%EnginesTargetPath%\D\*"
call :cpy "%BasePath%\DWorker\DWorker.exe" "%EnginesTargetPath%\D\*.bin"


rem -- Perl --

set BasePath=%SolutionDir%\RegexEngines\Perl
call :cpy "%BasePath%\PerlPlugin\bin\%Configuration%\%TargetDir%\PerlPlugin.dll" "%EnginesTargetPath%\Perl\*"
call :cpy "%BasePath%\PerlWorker\PerlWorker.pl" "%EnginesTargetPath%\Perl\*"
call :cpy /E "%BasePath%\PerlWorker\Perl-min\*" "%EnginesTargetPath%\Perl\Perl-min\*"


rem -- Fortran --

set BasePath=%SolutionDir%\RegexEngines\Fortran
call :cpy "%BasePath%\FortranPlugin\bin\%Configuration%\%TargetDir%\FortranPlugin.dll" "%EnginesTargetPath%\Fortran\*"
call :cpy "%BasePath%\ForgexWorker\x64\Release\FortranForgexWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"
call :cpy "%BasePath%\RegexJeyemhexWorker\x64\Release\FortranRegexJeyemhexWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"
call :cpy "%BasePath%\RegexPerazzWorker\x64\Release\FortranRegexPerazzWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"
call :cpy "%BasePath%\FregexWorker\x64\Release\FregexWorker.exe" "%EnginesTargetPath%\Fortran\*.bin"


rem -- TRE --

set BasePath=%SolutionDir%\RegexEngines\TRE
call :cpy "%BasePath%\TREPlugin\bin\%Configuration%\%TargetDir%\TREPlugin.dll" "%EnginesTargetPath%\TRE\*"
call :cpy "%BasePath%\TREWorker\bin\%Configuration%\%Platform%\TREWorker.exe" "%EnginesTargetPath%\TRE\*.bin"
call :cpy "%BasePath%\TRE\TRE\win32\bin\%Configuration%\%Platform%\tre.dll" "%EnginesTargetPath%\TRE\*"


rem -- tiny-regex-c --

set BasePath=%SolutionDir%\RegexEngines\TinyRegexC
call :cpy "%BasePath%\TinyRegexCPlugin\bin\%Configuration%\%TargetDir%\TinyRegexCPlugin.dll" "%EnginesTargetPath%\TinyRegexC\*"
call :cpy "%BasePath%\TinyRegexCWorker_Kokke\bin\%Configuration%\%Platform%\TinyRegexCWorker_Kokke.exe" "%EnginesTargetPath%\TinyRegexC\*.bin"
call :cpy "%BasePath%\TinyRegexCWorker_Rurban\bin\%Configuration%\%Platform%\TinyRegexCWorker_Rurban.exe" "%EnginesTargetPath%\TinyRegexC\*.bin"
call :cpy "%BasePath%\TinyRegexCWorker_Gyrovorbis\bin\%Configuration%\%Platform%\TinyRegexCWorker_Gyrovorbis.exe" "%EnginesTargetPath%\TinyRegexC\*.bin"


rem -- Ada --

set BasePath=%SolutionDir%\RegexEngines\Ada
call :cpy "%BasePath%\AdaPlugin\bin\%Configuration%\%TargetDir%\AdaPlugin.dll" "%EnginesTargetPath%\Ada\*"
call :cpy "%BasePath%\AdaWorker\adaworker.exe" "%EnginesTargetPath%\Ada\*.bin"


rem -- C++Builder --

set BasePath=%SolutionDir%\RegexEngines\CppBuilder
call :cpy "%BasePath%\CppBuilderPlugin\bin\%Configuration%\%TargetDir%\CppBuilderPlugin.dll" "%EnginesTargetPath%\CppBuilder\*"
call :cpy "%BasePath%\CppBuilderWorker\Win64\Release\CppBuilderWorker.exe" "%EnginesTargetPath%\CppBuilder\*.bin"


rem -- Qt --

set BasePath=%SolutionDir%\RegexEngines\Qt
call :cpy "%BasePath%\QtPlugin\bin\%Configuration%\%TargetDir%\QtPlugin.dll" "%EnginesTargetPath%\Qt\*"
call :cpy "%BasePath%\QtWorker\x64\Release\QtWorker.exe" "%EnginesTargetPath%\Qt\*.bin"
call :cpy "%BasePath%\QtWorker\x64\Release\Qt6Core.dll" "%EnginesTargetPath%\Qt\*"


rem -- CTRE --

set BasePath=%SolutionDir%\RegexEngines\CompileTimeRegex
call :cpy "%BasePath%\CompileTimeRegexPlugin\bin\%Configuration%\%TargetDir%\CompileTimeRegexPlugin.dll" "%EnginesTargetPath%\CompileTimeRegex\*"

call :cpy /E "%BasePath%\CompileTimeRegexSample\compile-time-regular-expressions\*" "%EnginesTargetPath%\CompileTimeRegex\CTRE\compile-time-regular-expressions\*"
call :cpy "%BasePath%\CompileTimeRegexSample\build.cmd" "%EnginesTargetPath%\CompileTimeRegex\CTRE\*"
call :cpy "%BasePath%\CompileTimeRegexSample\CompileTimeRegexSample.cpp" "%EnginesTargetPath%\CompileTimeRegex\CTRE\*"

call :cpy /E "%BasePath%\MRegexSample\mregex-min\*" "%EnginesTargetPath%\CompileTimeRegex\MRegex\mregex-min\*"
call :cpy "%BasePath%\MRegexSample\build.cmd" "%EnginesTargetPath%\CompileTimeRegex\MRegex\*"
call :cpy "%BasePath%\MRegexSample\MRegexSample.cpp" "%EnginesTargetPath%\CompileTimeRegex\MRegex\*"


rem -- GRETA --

set BasePath=%SolutionDir%\RegexEngines\GRETA
call :cpy "%BasePath%\GretaPlugin\bin\%Configuration%\%TargetDir%\GretaPlugin.dll" "%EnginesTargetPath%\GRETA\*"
call :cpy "%BasePath%\GretaWorker\bin\%Configuration%\%Platform%\GretaWorker.exe" "%EnginesTargetPath%\GRETA\*.bin"


rem -- Zig --

set BasePath=%SolutionDir%\RegexEngines\Zig
call :cpy "%BasePath%\ZigPlugin\bin\%Configuration%\%TargetDir%\ZigPlugin.dll" "%EnginesTargetPath%\Zig\*"
call :cpy "%BasePath%\ZigRegexWorker\zig-out\bin\ZigRegexWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\MvzrWorker\ZigMvzrWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\PzreWorker\zig-out\bin\ZigPzreWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\EziGexWorker\zig-out\bin\EziGexWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\Zoptia0regexWorker\zig-out\bin\Zoptia0regexWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\Z-RegexWorker\zig-out\bin\Z_RegexWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\ZRegexWorker\zig-out\bin\ZRegexWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\RegexZigWorker\zig-out\bin\RegexZigWorker.exe" "%EnginesTargetPath%\Zig\*.bin"
call :cpy "%BasePath%\SpectraWorker\zig-out\bin\SpectraWorker.exe" "%EnginesTargetPath%\Zig\*.bin"


rem -- Go --

set BasePath=%SolutionDir%\RegexEngines\Go
call :cpy "%BasePath%\GoPlugin\bin\%Configuration%\%TargetDir%\GoPlugin.dll" "%EnginesTargetPath%\Go\*"
call :cpy "%BasePath%\RegexpWorker\RegexpWorker.exe" "%EnginesTargetPath%\Go\*.bin"
call :cpy "%BasePath%\Regexp2Worker\Regexp2Worker.exe" "%EnginesTargetPath%\Go\*.bin"
call :cpy "%BasePath%\RexaWorker\RexaWorker.exe" "%EnginesTargetPath%\Go\*.bin"
call :cpy "%BasePath%\CoregexWorker\CoregexWorker.exe" "%EnginesTargetPath%\Go\*.bin"
call :cpy "%BasePath%\OnigmoGoRegexpWorker\OnigmoGoRegexpWorker.exe" "%EnginesTargetPath%\Go\*.bin"
call :cpy "%BasePath%\TreWorker\TreWorker.exe" "%EnginesTargetPath%\Go\*.bin"


rem -- Dart --

set BasePath=%SolutionDir%\RegexEngines\Dart
call :cpy "%BasePath%\DartPlugin\bin\%Configuration%\%TargetDir%\DartPlugin.dll" "%EnginesTargetPath%\Dart\*"
call :cpy "%BasePath%\RegExpWorker\RegExpWorker.exe" "%EnginesTargetPath%\Dart\*.bin"
call :cpy "%BasePath%\OnigurumaDartWorker\OnigurumaDartWorker.exe" "%EnginesTargetPath%\Dart\*.bin"
call :cpy "%BasePath%\IRegexpWorker\IRegexpWorker.exe" "%EnginesTargetPath%\Dart\*.bin"


rem -- REAL --

set BasePath=%SolutionDir%\RegexEngines\REAL
call :cpy "%BasePath%\RealPlugin\bin\%Configuration%\%TargetDir%\RealPlugin.dll" "%EnginesTargetPath%\REAL\*"
call :cpy "%BasePath%\RealWorker\bin\%Configuration%\%Platform%\RealWorker.exe" "%EnginesTargetPath%\REAL\*.bin"


goto :eof
:cpy
xcopy /D /R /Y %*| findstr /v /r /c:"^0 File\(s\) copied"
exit /b
