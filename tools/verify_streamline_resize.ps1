<#
.SYNOPSIS
Check that DLSS-G stays active after a Vulkan swapchain rebuild.
#>
param(
    [Parameter(Mandatory = $true)][string]$Repo,
    [int]$LoadWait = 40,
    [int]$PostResizeSeconds = 15
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

namespace DethraceTools {
    public static class WindowResize {
        [StructLayout(LayoutKind.Sequential)]
        private struct RECT {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetClientRect(IntPtr window, out RECT rect);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetWindowRect(IntPtr window, out RECT rect);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y,
            int width, int height, uint flags);

        public static bool SetClientSize(IntPtr window, int width, int height) {
            RECT client;
            RECT outer;
            if (!GetClientRect(window, out client) || !GetWindowRect(window, out outer))
                return false;

            int outerWidth = outer.Right - outer.Left + width - (client.Right - client.Left);
            int outerHeight = outer.Bottom - outer.Top + height - (client.Bottom - client.Top);
            const uint SWP_NOMOVE = 0x0002;
            const uint SWP_NOZORDER = 0x0004;
            const uint SWP_NOACTIVATE = 0x0010;
            return SetWindowPos(window, IntPtr.Zero, 0, 0, outerWidth, outerHeight,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}
'@

function Get-FgSamples([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    return @(Get-Content -LiteralPath $Path | ForEach-Object {
        if ($_ -match 'DLSS-G state status=(\d+) presented=(\d+) host-presents=(\d+)') {
            [pscustomobject]@{
                Status = [int]$Matches[1]
                Presented = [long]$Matches[2]
                HostPresents = [long]$Matches[3]
            }
        }
    })
}

function Get-SwapchainModes([string[]]$Paths) {
    return @($Paths | ForEach-Object {
        if (Test-Path -LiteralPath $_) {
            Get-Content -LiteralPath $_ | ForEach-Object {
                if ($_ -match 'VKREND: swapchain .* present mode (\d+)') {
                    [int]$Matches[1]
                }
            }
        }
    })
}

$repoPath = (Resolve-Path $Repo).Path
$exe = Join-Path $repoPath 'cmake-build-streamline/dethrace.exe'
$carma = Join-Path $repoPath 'Carma'
$referenceRoot = [IO.Path]::GetFullPath((Join-Path $repoPath 'staging/reference'))
$dest = [IO.Path]::GetFullPath((Join-Path $referenceRoot 'streamline-resize-validation'))
if (-not (Test-Path -LiteralPath $exe)) { throw "dethrace.exe not found at $exe" }
if (-not (Test-Path -LiteralPath $carma)) { throw "Carma/ not found under $repoPath" }
if ($LoadWait -lt 20) { throw 'LoadWait must be at least 20' }
if ($PostResizeSeconds -lt 5) { throw 'PostResizeSeconds must be at least 5' }
if (-not $dest.StartsWith($referenceRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "validation path is outside staging/reference: $dest"
}
if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
New-Item -ItemType Directory -Path $dest | Out-Null

$stdout = Join-Path $dest 'stdout.log'
$stderr = Join-Path $dest 'stderr.log'
$oldEnvironment = @{}
$settings = @{
    DETHRACE_ROOT_DIR = (Resolve-Path $carma).Path
    DETHRACE_VULKAN_SCENE_SIZE = '800x450'
    DETHRACE_STREAMLINE = '1'
    DETHRACE_DLSS = '1'
    DETHRACE_DLSSG = '1'
    DETHRACE_DLSS_MODE = 'performance'
}
foreach ($name in $settings.Keys) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $settings[$name], 'Process')
}

$args = @(
    '--vulkan',
    '--window',
    '--window-width=1600',
    '--window-height=900',
    '--quick-race-save',
    '-nosound',
    '-nocutscenes'
)
Write-Host "launch: dethrace $($args -join ' ')"
$proc = $null
try {
    $proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $repoPath `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    Start-Sleep -Seconds $LoadWait
    $proc.Refresh()
    if ($proc.HasExited) { throw "game exited before resize with code $($proc.ExitCode)" }
    $hwnd = $proc.MainWindowHandle
    if ($hwnd -eq [IntPtr]::Zero) { throw "no game window after $LoadWait seconds" }

    $beforeSamples = @(Get-FgSamples $stderr)
    if ($beforeSamples.Count -eq 0) { throw 'no DLSS-G count before resize' }
    $before = $beforeSamples[-1]
    $beforeModes = @(Get-SwapchainModes @($stdout, $stderr))
    if ($beforeModes.Count -eq 0) { throw 'no swapchain log before resize' }

    if (-not [DethraceTools.WindowResize]::SetClientSize($hwnd, 1598, 900)) {
        throw 'window resize failed'
    }

    $resizeDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 250
        $afterModes = @(Get-SwapchainModes @($stdout, $stderr))
    } while ($afterModes.Count -le $beforeModes.Count -and [DateTime]::UtcNow -lt $resizeDeadline)
    if ($afterModes.Count -le $beforeModes.Count) { throw 'resize did not rebuild the swapchain' }

    Start-Sleep -Seconds $PostResizeSeconds
    if ($proc.HasExited) { throw "game exited after resize with code $($proc.ExitCode)" }
    $afterSamples = @(Get-FgSamples $stderr)
    $after = $afterSamples[-1]
} finally {
    if ($null -ne $proc -and -not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(3000)) { $proc.Kill() }
    }
    foreach ($name in $settings.Keys) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
}

$hostDelta = $after.HostPresents - $before.HostPresents
$presentedDelta = $after.Presented - $before.Presented
if ($hostDelta -le 0) { throw 'no host presents after resize' }
$ratio = $presentedDelta / [double]$hostDelta
$initialMode = $beforeModes[0]
$rebuiltMode = $afterModes[-1]
$bad = @(Select-String -Path $stdout, $stderr `
    -Pattern 'fatal|assert|access violation|segmentation fault|vk.*failed' -CaseSensitive:$false)

Write-Host "present-mode: initial=$initialMode rebuilt=$rebuiltMode"
Write-Host "post-resize: presented=$presentedDelta host-presents=$hostDelta ratio=$($ratio.ToString('F2', [Globalization.CultureInfo]::InvariantCulture))"
Write-Host "errors: $($bad.Count)"
Write-Host "logs: $dest"
if ($initialMode -eq 2 -or $rebuiltMode -ne $initialMode) { throw 'swapchain rebuild changed the present mode' }
if ($ratio -lt 1.5) { throw 'DLSS-G did not stay active after resize' }
if ($bad.Count -ne 0) { throw 'renderer errors found in resize logs' }
