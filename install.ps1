<#
  Audio Ducker - quick installer.

  Downloads the latest release from GitHub, installs it, creates a desktop
  shortcut, registers it with Windows (Settings -> Apps -> Installed apps) and
  starts the app.
  Install location: %LOCALAPPDATA%\Programs\AudioDucker

  Quick start (paste into Windows PowerShell or any terminal):
    powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/Sahilpreetsinghvirdi/AudioDucker/main/install.ps1 | iex"

  Uninstall: Settings -> Apps -> Installed apps -> Audio Ducker -> Uninstall
  (the desktop shortcut and the install folder are removed automatically).
#>

$ErrorActionPreference = 'Stop'

$repo  = 'Sahilpreetsinghvirdi/AudioDucker'
$asset = 'AudioDucker-windows-x64.zip'
$dest  = Join-Path $env:LOCALAPPDATA 'Programs\AudioDucker'
$exe   = Join-Path $dest 'AudioDucker.exe'
$zip   = Join-Path $env:TEMP $asset
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioDucker'

try {
    # Stop a running copy so the files can be replaced.
    Get-Process AudioDucker -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue

    Write-Host 'Downloading Audio Ducker...' -ForegroundColor Cyan
    $url = "https://github.com/$repo/releases/latest/download/$asset"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Expand-Archive -Path $zip -DestinationPath $dest -Force

    # The archive may contain a top-level folder; locate the exe either way.
    $found = Get-ChildItem -Path $dest -Filter AudioDucker.exe -Recurse |
        Select-Object -First 1
    if (-not $found) { throw 'AudioDucker.exe not found in the downloaded archive.' }
    $exe = $found.FullName

    # Remove the "downloaded from the internet" mark so Windows runs it directly.
    Get-ChildItem $dest -Filter *.exe -Recurse | Unblock-File
    Remove-Item $zip -ErrorAction SilentlyContinue

    # Desktop shortcut.
    Write-Host 'Creating desktop shortcut...' -ForegroundColor Cyan
    $desktop = [Environment]::GetFolderPath('Desktop')
    $lnk = Join-Path $desktop 'Audio Ducker.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($lnk)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = Split-Path $exe
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Description = 'Automatically ducks background audio while you watch YouTube'
    $shortcut.Save()

    # Register with Windows so it shows in Settings -> Apps -> Installed apps
    # with a working uninstall entry.
    Write-Host 'Registering Audio Ducker with Windows...' -ForegroundColor Cyan
    $sizeBytes = (Get-ChildItem $dest -Recurse -File |
        Measure-Object -Property Length -Sum).Sum

    New-Item -Path $uninstallKey -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayName -Value 'Audio Ducker' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayVersion -Value '1.0.0' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name Publisher -Value 'Sahil Virdi' -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name DisplayIcon -Value $exe -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallLocation -Value $dest -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name InstallDate -Value (Get-Date -Format 'yyyyMMdd') -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name EstimatedSize -Value ([int]($sizeBytes / 1024)) -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallKey -Name UninstallString -Value ('powershell -NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $dest 'uninstall.ps1') + '"') -PropertyType String -Force | Out-Null

    # Uninstaller invoked from Settings -> Apps -> Installed apps.
    @'
# Audio Ducker uninstaller.
Get-Process AudioDucker -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

$key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\AudioDucker'
if (Test-Path $key) { Remove-Item -Path $key -Recurse -Force }

$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Audio Ducker.lnk'
if (Test-Path $lnk) { Remove-Item -Path $lnk -Force }

Remove-Item -LiteralPath $PSScriptRoot -Recurse -Force
'@ | Set-Content -Path (Join-Path $dest 'uninstall.ps1') -Encoding UTF8

    Write-Host 'Starting Audio Ducker...' -ForegroundColor Cyan
    Start-Process $exe

    Write-Host 'Audio Ducker is running.' -ForegroundColor Green
    Write-Host 'Right-click its speaker icon in the system tray to open Settings.'
    Write-Host 'Added to your desktop and to Settings -> Apps -> Installed apps.'
} catch {
    Write-Host "Install failed: $_" -ForegroundColor Red
    Write-Host 'Make sure a release with AudioDucker-windows-x64.zip exists at'
    Write-Host "https://github.com/$repo/releases"
    exit 1
}
