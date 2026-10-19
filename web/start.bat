@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js 18 or newer is required. Get it from https://nodejs.org/
  pause
  exit /b 1
)
echo Open http://127.0.0.1:8765 in your browser.
node server.js
pause
