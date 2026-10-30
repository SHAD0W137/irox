# apps/android/run.ps1
$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "      irox Engine -> Android Build       " -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# 1. Собираем APK через Gradle Wrapper
Write-Host "`n[1/3] Compiling C++ and Packaging APK..." -ForegroundColor Yellow
.\gradlew.bat assembleDebug

# 2. Устанавливаем APK на подключенный по USB телефон
Write-Host "`n[2/3] Installing APK to phone via ADB..." -ForegroundColor Yellow
adb install -r app/build/outputs/apk/debug/app-debug.apk

# 3. Запускаем приложение на экране смартфона
Write-Host "`n[3/3] Launching irox on Android..." -ForegroundColor Yellow
adb shell am start -n com.irox.engine/org.libsdl.app.SDLActivity

Write-Host "`n[SUCCESS] Engine is running on your phone!" -ForegroundColor Green