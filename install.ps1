<#
  Audio Ducker - quick installer.

  Downloads the latest release from GitHub and starts the app.
  Install location: %LOCALAPPDATA%\Programs\AudioDucker

  Quick start (paste into Windows PowerShell or any terminal):
    powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/Sahilpreetsinghvirdi/AudioDucker/main/install.ps1 | iex"

  Uninstall: close the app and delete the folder %LOCALAPPDATA%\Programs\AudioDucker
#>

$ErrorActionPreference = 'Stop'

$repo  = 'Sahilpreetsinghvirdi/AudioDucker'
$asset = 'AudioDucker-windows-x64.zip'
$dest  = Join-Path $env:LOCALAPPDATA 'Programs\AudioDucker'
$exe   = Join-Path $dest 'AudioDucker.exe'
$zip   = Join-Path $env:TEMP $asset

try {
    # Stop a running copy so the files can be replaced.
    Get-Process AudioDucker -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Write-Host 'Downloading Audio Ducker...' -ForegroundColor Cyan
    $url = "https://github.com/$repo/releases/latest/download/$asset"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Expand-Archive -Path $zip -DestinationPath $dest -Force

    # Remove the "downloaded from the internet" mark so Windows runs it directly.
    Get-ChildItem $dest -Filter *.exe | Unblock-File
    Remove-Item $zip -ErrorAction SilentlyContinue

    Write-Host 'Starting Audio Ducker...' -ForegroundColor Cyan
    Start-Process $exe

    Write-Host 'Audio Ducker is running.' -ForegroundColor Green
    Write-Host 'Right-click its speaker icon in the system tray to open Settings.'
} catch {
    Write-Host "Install failed: $_" -ForegroundColor Red
    Write-Host 'Make sure a release with AudioDucker-windows-x64.zip exists at'
    Write-Host "https://github.com/$repo/releases"
    exit 1
}
