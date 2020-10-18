%NW4C_ROOT%\snd\tool\SoundMaker\NW4C_SoundProjectConverter.exe -v -p 4 nwsound_project\simple.cspj

rmdir /y /q romfiles\sound
mkdir romfiles\sound

xcopy /e /y nwsound_project\output romfiles\sound

move romfiles\sound\simple.csid .
del romfiles\sound\simpleMap.html
del romfiles\sound\simple.xml