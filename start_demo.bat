@echo off
title Needlefish Wikipedia Live Server + Cloudflare Tunnel
echo ======================================================================
echo  Starting Needlefish C++ Search Engine (271,979 Wikipedia Articles)
echo ======================================================================
echo.

:: Start the Needlefish C++ server in the background
start "Needlefish Server" /B build\debug\needlefish.exe serve --index wikipedia.idx --port 8080 --web-dir web

:: Wait 1 second for socket bind
timeout /t 1 /nobreak >nul

echo Needlefish engine running on port 8080.
echo Starting Cloudflare Tunnel...
echo.

:: Start Cloudflare Tunnel
cloudflared.exe tunnel --url http://localhost:8080
