cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

cl "CompileTimeRegexSample.cpp" /nologo /Od /permissive- /GS /W3 /Zc:wchar_t /sdl /fp:precise /D "_WINDOWS" /D "_UNICODE" /D "UNICODE" /WX- /Zc:forScope /RTC1 /Gd /MT /std:c++20 /FC /EHsc /options:strict
::rem /Zc:inline /D "_DEBUG" /errorReport:prompt 
