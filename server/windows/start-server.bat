@echo off
rem Starts the GTA Local Online game server from the folder this file lives in.
rem Expected layout (like build\server-run):
rem   LocalOnlineServer.exe, server.toml, logs\, data\
cd /d "%~dp0"
if not exist server.toml (
  echo server.toml missing: copy config\server.example.toml here and edit it.
  pause
  exit /b 1
)
title GTA Local Online server
LocalOnlineServer.exe server.toml
echo.
echo Server stopped (exit code %errorlevel%). See logs\game-server.log
pause
