set tooldir=%CTRSDK_ROOT%\tools\CommandLineTools
%tooldir%\ctr_makecia32 -cci release\ctreq60.cci -o ctreq60.cia

set tooldir=%CTRSDK_ROOT%\tools\CommandLineTools
%tooldir%\ctr_makecia32 -cci release_nw\ctreq60_nw.cci -o ctreq60_nw.cia
