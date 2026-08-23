<#
.SYNOPSIS
Capture Stage 3 skid, smoke, and impact effects on the dry Race 0 start.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [ValidateSet('vulkan', 'opengl')][string]$Renderer = 'vulkan',
    [int]$LoadWait = 20
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameInput.ps1')

function Set-Key([string]$Name, [bool]$Up) {
    if (-not $Global:SCAN.ContainsKey($Name)) { throw "unknown key '$Name'" }
    [GI]::Key([uint16]$Global:SCAN[$Name], $Global:EXTENDED -contains $Name, $Up)
}

function Save-Burst([IntPtr]$Hwnd, [string]$Dir, [string]$Prefix, [int]$Count) {
    for ($i = 0; $i -lt $Count; $i++) {
        [void](Save-Shot $Hwnd $Dir ("{0}-{1:d2}" -f $Prefix, $i))
        Start-Sleep -Milliseconds 180
    }
}

function Apply-BodyworkDamage([IntPtr]$Hwnd, [int]$Count) {
    Send-Scan $Hwnd 'f4'
    for ($i = 0; $i -lt $Count; $i++) {
        Set-Key 'shift' $false
        Send-Scan $Hwnd '3'
        Set-Key 'shift' $true
    }
}

$repoPath = (Resolve-Path $Repo).Path
$exe = Join-Path $repoPath 'cmake-build-release/dethrace.exe'
$carma = Join-Path $repoPath 'Carma'
$referenceRoot = [IO.Path]::GetFullPath((Join-Path $repoPath 'staging/reference'))
$dest = [IO.Path]::GetFullPath((Join-Path $referenceRoot "stage3-$Renderer-effects-race0"))
if (-not (Test-Path $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path $carma)) { throw "Carma/ not found under $repoPath" }
if (-not $dest.StartsWith($referenceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "capture path is outside staging/reference: $dest"
}

if (Test-Path -LiteralPath $dest) {
    Remove-Item -LiteralPath $dest -Recurse -Force
}
New-Item -ItemType Directory -Path $dest | Out-Null

$env:DETHRACE_ROOT_DIR = (Resolve-Path $carma).Path
$stdout = Join-Path $dest 'stdout.log'
$stderr = Join-Path $dest 'stderr.log'
$args = @(
    "--$Renderer",
    '--quick-race=0',
    '--window',
    '--window-width=800',
    '--window-height=600',
    '--i-am-cheating'
)

$Global:SCAN['f4'] = 0x3e
$Global:SCAN['shift'] = 0x2a
$Global:SCAN['3'] = 0x04

Write-Host "launch: dethrace $($args -join ' ')"
$proc = Start-Process -FilePath $exe -ArgumentList $args -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
try {
    Start-Sleep -Seconds $LoadWait
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no game window after $LoadWait seconds" }

    Hold-Scan $hwnd 'kp8' 3500
    Hold-Scan $hwnd 'kp4' 1000
    [void](Save-Shot $hwnd $dest '00-before-skid')

    Focus-Game $hwnd
    Set-Key 'kp8' $false
    Set-Key 'kp4' $false
    Set-Key 'space' $false
    Save-Burst $hwnd $dest 'skid' 8
    Set-Key 'space' $true
    Set-Key 'kp4' $true
    Set-Key 'kp8' $true

    Apply-BodyworkDamage $hwnd 4
    Save-Burst $hwnd $dest 'smoke' 8

    Hold-Scan $hwnd 'kp8' 5000
    Save-Burst $hwnd $dest 'impact' 14
} finally {
    if (-not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(3000)) { $proc.Kill() }
    }
}

$bad = Select-String -Path $stdout, $stderr -Pattern 'validation error|fatal|assert|access violation|segmentation fault' -CaseSensitive:$false
$pngCount = @(Get-ChildItem -LiteralPath $dest -Filter '*.png').Count
Write-Host "capture: $dest"
Write-Host "png files: $pngCount"
if ($bad) { throw "renderer errors found in capture logs" }
if ($pngCount -ne 31) { throw "expected 31 PNG files, got $pngCount" }
