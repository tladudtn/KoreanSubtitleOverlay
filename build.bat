@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
msbuild KoreanSubtitleOverlay.vcxproj /p:Configuration=Release /p:Platform=Win32 /t:Rebuild
