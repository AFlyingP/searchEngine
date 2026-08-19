# Needlefish Wikipedia Live Server + Cloudflare Tunnel Launcher
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Starting Needlefish C++ Search Engine (271,979 Wikipedia Articles)" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host ""

$server = Start-Process -FilePath ".\build\debug\needlefish.exe" -ArgumentList "serve --index wikipedia.idx --port 8080 --web-dir web" -PassThru -NoNewWindow
Start-Sleep -Seconds 1

Write-Host "Needlefish engine is running on port 8080." -ForegroundColor Green
Write-Host "Starting Cloudflare Tunnel to expose live Wikipedia engine..." -ForegroundColor Yellow
Write-Host ""

try {
    .\cloudflared.exe tunnel --url http://localhost:8080
} finally {
    if ($server -and !$server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}
