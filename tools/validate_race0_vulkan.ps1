<#
.SYNOPSIS
Run a moving Vulkan Race 0 stability pass with redirected validation logs.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [int]$Seconds = 60,
    [int]$LoadWait = 20,
    [double]$RenderScale = 1.0
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GameInput.ps1')

$repoPath = (Resolve-Path $Repo).Path
$exe = Join-Path $repoPath 'cmake-build-release/dethrace.exe'
$carma = Join-Path $repoPath 'Carma'
$dest = [IO.Path]::GetFullPath((Join-Path $repoPath 'staging/reference/stage3-vulkan-race0-validation'))
$referenceRoot = [IO.Path]::GetFullPath((Join-Path $repoPath 'staging/reference'))
if (-not (Test-Path $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path $carma)) { throw "Carma/ not found under $repoPath" }
if ($Seconds -lt 20) { throw 'Seconds must be at least 20' }
if ($RenderScale -lt 0.5 -or $RenderScale -gt 1.0) { throw 'RenderScale must be between 0.5 and 1.0' }
if (-not $dest.StartsWith($referenceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "validation path is outside staging/reference: $dest"
}
if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
New-Item -ItemType Directory -Path $dest | Out-Null

$env:DETHRACE_ROOT_DIR = (Resolve-Path $carma).Path
$oldRenderScale = $env:DETHRACE_VULKAN_RENDER_SCALE
$env:DETHRACE_VULKAN_RENDER_SCALE = $RenderScale.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
$args = @('--vulkan', '--quick-race=0', '--window', '--window-width=800', '--window-height=600')
$stdout = Join-Path $dest 'stdout.log'
$stderr = Join-Path $dest 'stderr.log'
Write-Host "launch: dethrace $($args -join ' ')"
$proc = Start-Process -FilePath $exe -ArgumentList $args -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
try {
    Start-Sleep -Seconds $LoadWait
    $proc.Refresh()
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no game window after $LoadWait seconds" }

    $remaining = $Seconds * 1000
    $turn = 0
    while ($remaining -gt 0 -and -not $proc.HasExited) {
        Hold-Scan $hwnd 'kp8' 3500
        Send-Scan $hwnd ($(if (($turn++ % 2) -eq 0) { 'kp4' } else { 'kp6' })) 500
        $remaining -= 4000
    }
} finally {
    if (-not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(3000)) { $proc.Kill() }
    }
    if ($null -eq $oldRenderScale) {
        Remove-Item Env:DETHRACE_VULKAN_RENDER_SCALE -ErrorAction SilentlyContinue
    } else {
        $env:DETHRACE_VULKAN_RENDER_SCALE = $oldRenderScale
    }
}

$bad = @(Select-String -Path $stdout, $stderr -Pattern 'validation error|vkrend: \[error\]|vuid-|fatal|assert|access violation|segmentation fault|vk.*failed' -CaseSensitive:$false)
$race = @(Select-String -Path $stdout, $stderr -Pattern 'Quick race enabled, starting race 0' -CaseSensitive:$false)
Write-Host "race0-start-lines: $($race.Count)"
Write-Host "validation-errors: $($bad.Count)"
Write-Host "logs: $dest"
if ($race.Count -ne 1) { throw 'Race 0 was not selected' }
if ($bad.Count -ne 0) { throw 'renderer errors found in validation logs' }
