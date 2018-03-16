@echo off

REM *********************************
set GCC=gcc -std=c99 -o3
set TCL=83
set TCLDIR=\DEV\TCL\TCL%TCL
set WINLIB=-lwinmm
REM set TCLLIB=-ltclstub%TCL -ltkStub%TCL
set TCLLIB=-ltclstub%TCL
set FILE=matzsf
set FILEadd=minisdl_audio.c
set EXT=dll
set PCK=\win-apps\tools\upx393.exe -9

%GCC -shared -o %FILE.%EXT %FILE.c %FILEadd -DUSE_TCL_STUBS -DUSE_TK_STUBS %WINLIB -I%TCLDIR\include -L%TCLDIR\lib %TCLLIB

strip %FILE.%EXT
%PCK %FILE.%EXT
