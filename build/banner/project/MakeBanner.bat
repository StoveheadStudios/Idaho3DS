pushd %~dp0

SET bannerexe="%CTRSDK_ROOT%\tools\CommandLineTools\ctr_makebanner32.exe"
SET modelexe="%CTRSDK_ROOT%\tools\CommandLineTools\ctr_BannerModelConverter32.exe"
SET iconexe="%CTRSDK_ROOT%\tools\CommandLineTools\ctr_TexturePackager32.exe"
SET soundexe="%CTRSDK_ROOT%\tools\CommandLineTools\ctr_WaveConverter32.exe"

::cleanup
rmdir /s /q .temp
mkdir .temp

:: Convert the icon files
%iconexe% -dsl -l big.tga -o .temp\big.ctpk
%iconexe% -dsl -l little.tga -o .temp\little.ctpk

:: Convert the sound file
%soundexe% -o .temp\sound.cbsd sound.wav

:: Convert the banner model file
%modelexe% BannerModel .temp\model.cbmd

:: Make the banner file
mkdir ..\export
%bannerexe% -d banner.bsf ..\export\banner.bnr ..\export\banner.icn

::cleanup
rmdir /s /q .temp

popd