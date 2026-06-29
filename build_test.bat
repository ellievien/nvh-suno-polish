@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\nvhstudio\VocalCleanerVST"
set "RN=C:\Users\nvhstudio\rnnoise"
if not exist obj mkdir obj
echo === COMPILE ===
cl /nologo /O1 /EHsc /D_USE_MATH_DEFINES /D_CRT_SECURE_NO_WARNINGS ^
   /I "%RN%\include" /I "Source" /I "%RN%\src" /Fo"obj\\" ^
   test\test_core.cpp ^
   "%RN%\src\denoise.c" "%RN%\src\rnn.c" "%RN%\src\pitch.c" "%RN%\src\kiss_fft.c" ^
   "%RN%\src\celt_lpc.c" "%RN%\src\nnet.c" "%RN%\src\nnet_default.c" ^
   "%RN%\src\parse_lpcnet_weights.c" "%RN%\src\rnnoise_data.c" "%RN%\src\rnnoise_tables.c" ^
   /Fe:test_core.exe || exit /b 1
echo === RUN ===
test_core.exe
