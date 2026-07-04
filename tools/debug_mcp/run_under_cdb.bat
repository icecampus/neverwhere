@echo off
setlocal
cd /D D:\campus\neverwhere
set PATH=D:\campus\neverwhere\_intermediate_64\debug;%PATH%
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" ^
  -G ^
  -y "srv*;D:\campus\neverwhere\_intermediate_64\debug" ^
  -cf:tools\debug_mcp\cdb_draw_pencil.txt ^
  _intermediate_64\debug\EpicMapEditor.exe
endlocal
