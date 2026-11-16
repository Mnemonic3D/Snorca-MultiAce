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
        @{ package=$label; title=$process.MainWindowTitle; window=$process.MainWindowHandle.ToInt64(); fresh_profile=$profile; binary_sha256=(Get-FileHash $exe).Hash; scope='Startup only; screenshot needs review; no physical printer test' } | ConvertTo-Json | Set-Content "$evidence/$label.json"
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
