set INCLUDE_DIRECTORY=%~1

cd /d "%~dp0"

set bat_path="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %bat_path% goto compile

set bat_path="D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %bat_path% goto compile

set bat_path="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %bat_path% goto compile

set bat_path="D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %bat_path% goto compile

echo Error: Cannot find 'vcvars64.bat'
goto exit

:compile

if "%INCLUDE_DIRECTORY%"=="" set "INCLUDE_DIRECTORY=.\mregex-min\include"

call %bat_path%
cl "MRegexSample.cpp" /nologo /Od /permissive- /GS /W3 /Zc:wchar_t /fp:precise /D "_WINDOWS" /D "_UNICODE" /D "UNICODE" /I"%INCLUDE_DIRECTORY%" /WX- /Zc:forScope /RTC1 /Gd /MD /std:c++20 /EHsc /options:strict
goto exit

:exit
