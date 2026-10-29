@echo off
rem Build the WPF settings launcher into build\amped3_launcher.exe (.NET Framework csc).
cd /d "%~dp0.."
set FW=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319
set WPF=%FW%\WPF
"%FW%\csc.exe" /nologo /target:winexe /optimize+ /out:build\amped3_launcher.exe ^
  /r:"%WPF%\PresentationFramework.dll" /r:"%WPF%\PresentationCore.dll" /r:"%WPF%\WindowsBase.dll" ^
  /r:System.Xaml.dll project\launcher\Launcher.cs || exit /b 1
echo LAUNCHER OK
