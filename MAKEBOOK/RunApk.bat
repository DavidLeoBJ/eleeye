@echo off

set ROOT=D:\AndroidStudioProjects\ChineseChessSpectator
set ASSETS=%ROOT%\appb\src\main\assets

copy /Y "D:\AndroidStudioProjects\eleeye\MAKEBOOK\micro.book" "%ASSETS%\micro.book"

cd /d %ROOT%
call gradlew :appb:clean :appb:assembleDebug

adb install -r %ROOT%\appb\build\outputs\apk\debug\appb-debug.apk

REM 下面的命令用于清除应用数据
adb shell pm clear com.example.appb

REM 启动测试命令可以无需要知道包名activity名
REM adb shell monkey -p com.example.appb 1
REM 在较新Android中也可以指定包名adb shell am start -p com.example.appb
REM 指定Activity最规范：
adb shell am start -n com.example.appb/.MainActivity
pause