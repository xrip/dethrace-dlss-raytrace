<#
.SYNOPSIS
Capture a reproducible in-game reference screenshot set from a dethrace build.

Stage 0 baseline for the Vulkan port: stages 1-3 are compared against what this
produces from the current OpenGL and software renderers.

Uses --quick-race, so the game loads straight into a race with no menu input at
all. Driving the front-end with synthetic keystrokes was unreliable and is not
worth reviving -- if you need a different track, pass -Race.

Two things that will waste your time if changed:

  * In-race input goes through SendInput with hardware scancodes
    (tools/GameInput.ps1). WScript.Shell SendKeys does NOT work: SDL reads raw
    scancodes, and Windows blocks SetForegroundWindow from a background process,
    so presses land in the calling terminal instead of the game.
  * Run windowed. Fullscreen captures come back black -- the swap chain never
    reaches the desktop compositor. (DPI is handled in GameInput.ps1; this
    display is 3840x2160 at 150% and GDI disagrees by 1.5x without it.)

Each shot logs a fingerprint. Identical consecutive fingerprints mean input
stopped landing rather than that the scene is static.

.EXAMPLE
  ./tools/capture_reference.ps1 -Tag opengl -GameArgs '--opengl' -Repo <repo>
  ./tools/capture_reference.ps1 -Tag software -Repo <repo>
#>
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string[]]$GameArgs = @(),
    [Parameter(Mandatory = $true)][string]$Repo,
    [int]$Race = 0,
    [int]$Width = 800,
    [int]$Height = 600,
    [int]$LoadWait = 20
)

. (Join-Path $PSScriptRoot 'GameInput.ps1')

$repo = (Resolve-Path $Repo).Path
$exe = Join-Path $repo 'cmake-build-release/dethrace.exe'
if (-not (Test-Path $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path (Join-Path $repo 'Carma'))) { throw "Carma/ not found under $repo" }

$dest = Join-Path $repo "staging/reference/$Tag"
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dest | Out-Null

$env:DETHRACE_ROOT_DIR = (Resolve-Path (Join-Path $repo 'Carma')).Path
$all = @($GameArgs) + @("--quick-race=$Race", '--window', "--window-width=$Width", "--window-height=$Height")
Write-Host "launch: dethrace $($all -join ' ')"
$proc = Start-Process -FilePath $exe -ArgumentList $all -PassThru
Start-Sleep -Seconds $LoadWait
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { $proc.Kill(); throw "no game window after ${LoadWait}s" }

$prints = [ordered]@{}
function Step($name) { $prints[$name] = Save-Shot $hwnd $dest $name }

Step '00-race-start'

# Drive so there is motion and scenery streaming past. Accelerate is numpad 8,
# NOT arrow Up -- see the note in GameInput.ps1.
Hold-Scan $hwnd 'kp8' 4000; Step '01-chase-moving'
Hold-Scan $hwnd 'kp8' 4000; Step '02-chase-moving2'

# Distinct render paths (key defaults from src/DETHRACE/constants.h:283-291).
Send-Scan $hwnd 'c';   Start-Sleep -Seconds 3; Step '03-cockpit'
Send-Scan $hwnd 'm';   Start-Sleep -Seconds 3; Step '04-cockpit-mirror'
Send-Scan $hwnd 'm';   Start-Sleep -Seconds 2
Send-Scan $hwnd 'c';   Start-Sleep -Seconds 2; Step '05-chase-again'
Send-Scan $hwnd 'tab'; Start-Sleep -Seconds 3; Step '06-map'
Send-Scan $hwnd 'tab'; Start-Sleep -Seconds 3; Step '07-back-to-race'

# Drive into scenery to exercise the smoke / damage / particle paths.
Hold-Scan $hwnd 'kp8' 6000; Start-Sleep -Seconds 2; Step '08-after-impact'

if (-not $proc.HasExited) { $proc.Kill() }

$names = @($prints.Keys)
$stalled = @()
for ($i = 1; $i -lt $names.Count; $i++) {
    if ($null -ne $prints[$names[$i]] -and $prints[$names[$i]] -eq $prints[$names[$i - 1]]) {
        $stalled += "$($names[$i-1]) == $($names[$i])"
    }
}
if ($stalled) {
    Write-Host "WARNING identical consecutive frames (input may have been lost):"
    $stalled | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "all frames distinct"
}
Write-Host "reference set: $dest"
