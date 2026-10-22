@echo off
rem Starts GodEye (Phase 9) next to the game server. Needs Node.js 24+ (runs the TypeScript sources directly).
rem Config: godeye\backend\godeye.json (copy godeye.example.json). The map data comes from IslandBuilder godeyemap.
cd /d "%~dp0..\..\godeye\backend"
title GodEye
node src\main.ts godeye.json
echo.
echo GodEye stopped (exit code %errorlevel%).
pause
