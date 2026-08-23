<#
.SYNOPSIS
Check that Vulkan renders a live BRender model update.

.DESCRIPTION
Starts a quick race, takes a shot, and uses the game's own cheat edit mode to
apply power-up 13 (trashed bodywork). It then takes a second shot and reports
the changed-pixel share in the car area. BRender rebuilds stored geometry after
BrModelUpdate(), so a new stored object is also a valid live model update.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [int]$LoadWait = 20
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameInput.ps1')

function Compare-CarArea([string]$beforePath, [string]$afterPath) {
    $beforeImage = [System.Drawing.Bitmap]::new($beforePath)
    $afterImage = [System.Drawing.Bitmap]::new($afterPath)
    try {
        if ($beforeImage.Size -ne $afterImage.Size) { throw 'shot sizes differ' }
        $left = [int]($beforeImage.Width * 0.30)
        $right = [int]($beforeImage.Width * 0.72)
        $top = [int]($beforeImage.Height * 0.48)
        $bottom = [int]($beforeImage.Height * 0.88)
        $changed = 0
        $total = 0
        for ($y = $top; $y -lt $bottom; $y += 2) {
            for ($x = $left; $x -lt $right; $x += 2) {
                $a = $beforeImage.GetPixel($x, $y)
                $b = $afterImage.GetPixel($x, $y)
                $delta = [Math]::Abs($a.R - $b.R) + [Math]::Abs($a.G - $b.G) + [Math]::Abs($a.B - $b.B)
                if ($delta -gt 48) { $changed++ }
                $total++
            }
        }
        return $changed / $total
    } finally {
        $beforeImage.Dispose()
        $afterImage.Dispose()
    }
}

$repo = (Resolve-Path $Repo).Path
$exe = Join-Path $repo 'cmake-build-release/dethrace.exe'
$carma = Join-Path $repo 'Carma'
$dest = Join-Path $repo 'staging/reference/vulkan-deformation'
if (-not (Test-Path $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path $carma)) { throw "Carma/ not found under $repo" }

if (Test-Path -LiteralPath $dest) {
    Remove-Item -LiteralPath $dest -Recurse -Force
}
New-Item -ItemType Directory -Path $dest | Out-Null

$env:DETHRACE_ROOT_DIR = (Resolve-Path $carma).Path
$stdout = Join-Path $dest 'stdout.log'
$stderr = Join-Path $dest 'stderr.log'
$args = @(
    '--vulkan',
    '--quick-race=0',
    '--window',
    '--window-width=800',
    '--window-height=600',
    '--i-am-cheating'
)

$proc = Start-Process -FilePath $exe -ArgumentList $args `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
try {
    Start-Sleep -Seconds $LoadWait
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no game window after ${LoadWait}s" }

    $before = Save-Shot $hwnd $dest '00-before-damage'

    # F4 selects the option edit mode. Shift+3 becomes power-up 13 in
    # GotPowerupN(). POWERUP.TXT maps it to the real TrashBodywork game path.
    $Global:SCAN['f4'] = 0x3e
    $Global:SCAN['shift'] = 0x2a
    $Global:SCAN['3'] = 0x04
    Send-Scan $hwnd 'f4'
    Start-Sleep -Milliseconds 500
    $null = Save-Shot $hwnd $dest '01-edit-mode'
    Focus-Game $hwnd
    [GI]::Key([uint16]$Global:SCAN['shift'], $false, $false)
    Start-Sleep -Milliseconds 100
    [GI]::Key([uint16]$Global:SCAN['3'], $false, $false)
    Start-Sleep -Milliseconds 120
    [GI]::Key([uint16]$Global:SCAN['3'], $false, $true)
    Start-Sleep -Milliseconds 100
    [GI]::Key([uint16]$Global:SCAN['shift'], $false, $true)
    Start-Sleep -Milliseconds 500
    $null = Save-Shot $hwnd $dest '02-powerup-message'
    Start-Sleep -Seconds 4

    $after = Save-Shot $hwnd $dest '03-after-damage'
    Write-Host "fingerprints before=$before after=$after changed=$($before -ne $after)"
} finally {
    if (-not $proc.HasExited) {
        $null = $proc.CloseMainWindow()
        Start-Sleep -Seconds 2
        if (-not $proc.HasExited) { $proc.Kill() }
    }
    $proc.WaitForExit()
}

$log = @()
if (Test-Path $stdout) { $log += Get-Content $stdout }
if (Test-Path $stderr) { $log += Get-Content $stderr }
$signals = @($log | Where-Object {
        $_ -match 'live vertex update|VKREND.*\[(warn|error)\]|fatal|crash'
    })
$liveUpdate = @($signals | Where-Object { $_ -match 'live vertex update' })
$failures = @($signals | Where-Object { $_ -notmatch 'live vertex update' })
$carChange = Compare-CarArea (Join-Path $dest '00-before-damage.png') (Join-Path $dest '03-after-damage.png')

Write-Host ("exit={0} live-update-log={1} car-change={2:P1} failures={3}" -f `
        $proc.ExitCode, $liveUpdate.Count, $carChange, $failures.Count)
$signals | Select-Object -Last 30

if ($proc.ExitCode -ne 0) { throw "game exited with code $($proc.ExitCode)" }
if ($carChange -lt 0.10) { throw 'the Vulkan-rendered car did not change enough' }
if ($failures.Count -ne 0) { throw 'Vulkan validation or runtime errors were found' }
