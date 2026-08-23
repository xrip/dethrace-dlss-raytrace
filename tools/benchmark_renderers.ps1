<#
.SYNOPSIS
Record opt-in presentation FPS for Vulkan and OpenGL on the same dry Race 0.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [int]$Seconds = 10,
    [int]$Width = 800,
    [int]$Height = 600
)

$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path $Repo).Path
$exe = Join-Path $repoPath 'cmake-build-release/dethrace.exe'
$carma = Join-Path $repoPath 'Carma'
$referenceRoot = [IO.Path]::GetFullPath((Join-Path $repoPath 'staging/reference'))
if (-not (Test-Path $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path $carma)) { throw "Carma/ not found under $repoPath" }
if ($Seconds -lt 3) { throw 'Seconds must be at least 3' }

$env:DETHRACE_ROOT_DIR = (Resolve-Path $carma).Path
$results = @()
foreach ($renderer in @('vulkan', 'opengl')) {
    $dest = [IO.Path]::GetFullPath((Join-Path $referenceRoot "stage3-fps-$renderer"))
    if (-not $dest.StartsWith($referenceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "benchmark path is outside staging/reference: $dest"
    }
    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    New-Item -ItemType Directory -Path $dest | Out-Null

    $stats = Join-Path $dest 'frame-stats.log'
    $env:DETHRACE_FRAME_STATS = $stats
    $args = @(
        "--$renderer",
        '--quick-race=0',
        '--fps=0',
        '--vsync=0',
        '--window',
        "--window-width=$Width",
        "--window-height=$Height"
    )
    Write-Host "launch: dethrace $($args -join ' ')"
    $proc = Start-Process -FilePath $exe -ArgumentList $args -RedirectStandardOutput (Join-Path $dest 'stdout.log') -RedirectStandardError (Join-Path $dest 'stderr.log') -PassThru
    try {
        Start-Sleep -Seconds $Seconds
    } finally {
        if (-not $proc.HasExited) {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit(3000)) { $proc.Kill() }
        }
    }

    if (-not (Test-Path $stats)) { throw "no frame stats for $renderer" }
    $samples = @(Get-Content -LiteralPath $stats | ForEach-Object {
        if ($_ -match 'elapsed_ms=(\d+)\s+frames=(\d+)\s+fps=([0-9.]+)') {
            [pscustomobject]@{
                ElapsedMs = [double]$Matches[1]
                Frames = [int]$Matches[2]
                Fps = [double]$Matches[3]
            }
        }
    })
    if ($samples.Count -eq 0) { throw "empty frame stats for $renderer" }
    $elapsed = ($samples | Measure-Object -Property ElapsedMs -Sum).Sum
    $frames = ($samples | Measure-Object -Property Frames -Sum).Sum
    $average = $frames * 1000.0 / $elapsed
    $averageText = $average.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
    $line = "$renderer frames=$frames elapsed_ms=$([int]$elapsed) average_fps=$averageText"
    $line | Tee-Object -FilePath (Join-Path $dest 'summary.txt')
    $results += $line
}
Remove-Item Env:DETHRACE_FRAME_STATS -ErrorAction SilentlyContinue

Write-Host ''
Write-Host 'Renderer FPS summary (Race 0, --fps=0, --vsync=0):'
$results | ForEach-Object { Write-Host $_ }
