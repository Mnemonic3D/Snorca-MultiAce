# Runs only on an expendable Windows CI runner, never on a user's installation.
$ErrorActionPreference = 'Stop'
$evidence = Join-Path $PWD 'package-test-results'
New-Item -ItemType Directory -Force $evidence | Out-Null
$source = (Resolve-Path 'build/Snapmaker_Orca').Path

function Test-Package($root, $label) {
    # Check every shipped runtime web asset against the build, including nested include/lib.
    $web = Join-Path $source 'resources/web'
    foreach ($file in Get-ChildItem $web -Recurse -File | Where-Object { $_.Extension -in '.js','.css','.json','.html','.wasm','.woff','.woff2','.ttf','.svg','.png' }) {
        $relative = $file.FullName.Substring($source.Length).TrimStart([char]92,[char]47)
        $target = Join-Path $root $relative
        if (-not (Test-Path $target)) { throw "$label missing runtime asset $relative" }
        if ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash $target).Hash) { throw "$label changed runtime asset $relative" }
    }
    $exe = Join-Path $root 'FOrcaSlicer.exe'
    $profile = Join-Path $env:RUNNER_TEMP "$label-empty-profile"
    if (Test-Path $profile) { throw "Profile must start empty: $profile" }
    $process = Start-Process $exe -ArgumentList @('--datadir', ('"' + $profile + '"')) -PassThru -WorkingDirectory $root -RedirectStandardOutput "$evidence/$label-stdout.txt" -RedirectStandardError "$evidence/$label-stderr.txt"
    try {
        Start-Sleep -Seconds 30
        $process.Refresh()
        if ($process.HasExited) { throw "$label exited early: $($process.ExitCode)" }
        if ($process.MainWindowHandle -eq 0) { throw "$label has no main window" }
        Add-Type -AssemblyName System.Windows.Forms
        Add-Type -AssemblyName System.Drawing
        $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bitmap.Size)
        $bitmap.Save("$evidence/$label-desktop.png")
        $graphics.Dispose()
        $bitmap.Dispose()
        # Ctrl+N is the application's New Project shortcut and opens Prepare.
        $shell = New-Object -ComObject WScript.Shell
        if (-not $shell.AppActivate($process.Id)) { throw "Could not activate $label window" }
        [System.Windows.Forms.SendKeys]::SendWait('^n')
        Start-Sleep -Seconds 15
        $process.Refresh()
        if ($process.HasExited) { throw "$label exited while opening Prepare" }
        $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bitmap.Size)
        $bitmap.Save("$evidence/$label-prepare.png")
        $graphics.Dispose()
        $bitmap.Dispose()
        @{ package=$label; title=$process.MainWindowTitle; window=$process.MainWindowHandle.ToInt64(); fresh_profile=$profile; binary_sha256=(Get-FileHash $exe).Hash; core_sha256=(Get-FileHash (Join-Path $root 'FOrcaSlicer-core.dll')).Hash; scope='Home and Prepare startup; screenshots need review; no physical printer test' } | ConvertTo-Json | Set-Content "$evidence/$label.json"
    } finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        Get-Process snapmaker_control_windows_x64 -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}

$zip = Get-ChildItem dist -Filter '*.zip' | Select-Object -First 1
$portable = Join-Path $env:RUNNER_TEMP 'Snorca portable fresh'
Expand-Archive -LiteralPath $zip.FullName -DestinationPath $portable
Test-Package (Join-Path $portable 'FOrcaSlicer') 'portable'
$installer = Get-ChildItem dist -Filter '*.exe' | Select-Object -First 1
$installed = Join-Path $env:RUNNER_TEMP 'Snorca installed fresh'
$setup = Start-Process $installer.FullName -ArgumentList "/S /D=$installed" -PassThru -Wait
if ($setup.ExitCode -ne 0) { throw "Installer failed: $($setup.ExitCode)" }
Test-Package $installed 'installer'

# Simulate an update over an existing install whose uninstaller is missing.
# The recorded custom install location must be reused without /D.
$legacyAsset = Join-Path $installed 'resources/web/flutter_web/posthog-array.0930d984aecf936d75a80e534e55d1eb.js'
$index = Join-Path $installed 'resources/web/flutter_web/index.html'
$expectedIndexHash = (Get-FileHash $index).Hash
Set-Content $legacyAsset 'obsolete bundle asset'
Set-Content $index 'older installed application file'
Remove-Item (Join-Path $installed 'Uninstall.exe')
$portableData = Join-Path $installed 'data_dir'
New-Item -ItemType Directory -Force $portableData | Out-Null
$profileMarker = Join-Path $portableData 'upgrade-profile-marker.txt'
$projectMarker = Join-Path $installed 'upgrade-project-marker.3mf'
Set-Content $profileMarker 'original portable settings'
Set-Content $projectMarker 'original project'
$profileHash = (Get-FileHash $profileMarker).Hash
$projectHash = (Get-FileHash $projectMarker).Hash

$setup = Start-Process $installer.FullName -ArgumentList '/S' -PassThru -Wait
if ($setup.ExitCode -ne 0) { throw "In-place update failed: $($setup.ExitCode)" }
if ((Get-FileHash $index).Hash -ne $expectedIndexHash) { throw 'Installer did not replace the old application file in the registered folder' }
if (Test-Path $legacyAsset) { throw 'Installer left a known obsolete web asset behind' }
if (-not (Test-Path (Join-Path $installed 'Uninstall.exe'))) { throw 'Installer did not recreate its uninstaller' }
if ((Get-FileHash $profileMarker).Hash -ne $profileHash) { throw 'Installer changed the portable profile' }
if ((Get-FileHash $projectMarker).Hash -ne $projectHash) { throw 'Installer changed the user project' }
@{ location_reused=$true; old_uninstaller_missing=$true; obsolete_file_removed=$true; installed_file_replaced=$true; profile_preserved=$true; project_preserved=$true } |
    ConvertTo-Json | Set-Content "$evidence/in-place-upgrade.json"
