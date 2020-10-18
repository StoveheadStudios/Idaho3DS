pushd %~dp0

rem make banner
cd banner\project
call makebanner.bat
cd ..\..

set tooldir=%CTRSDK_ROOT%\tools\CommandLineTools

rmdir /s /q .temp
mkdir .temp

set outdir="..\Release_NW"
set specfile=%tooldir%\..\..\resources\specfiles\Application.desc
set iconfile=banner\export\banner.icn
set bannerfile=banner\export\banner.bnr
set OFILE=ctreq60
set manfile=ctreq60.cfa
set RSF=..\ctreq60_NW.rsf

del %OFILE%.cia
%tooldir%\ctr_makerom32.exe -DROMFS_ROOT=manual\export -f data -o %manfile% -rsf manual\manual.rsf
%tooldir%\ctr_makerom32.exe %outdir%\ctreq60_NW.axf -rsf %RSF% -desc %specfile% -f exec -DTITLE=ctreq60 -DROMFS_ROOT=../romfiles -icon %iconfile% -banner %bannerfile% -o %OFILE%.cxi
%tooldir%\ctr_makecia32 -i %OFILE%.cxi -man %manfile% -o %OFILE%.cia
del %OFILE%.cxi
del %OFILE%.cxi.xml
del %manfile%.xml
del %manfile%

rmdir /s /q .temp

popd