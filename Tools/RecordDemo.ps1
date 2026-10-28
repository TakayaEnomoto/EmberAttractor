# Records the scripted showcase (-EmberDemo) for the portfolio video.
# The 1920x1080 game client area is centred on the chosen monitor and only that rectangle is
# captured (no window frame, no screen edges, no cursor). Requires an ffmpeg with gdigrab.
param(
    [int]$Monitor = 1,                 # index into [Windows.Forms.Screen]::AllScreens
    [string]$Output = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'Video\EmberAttractor_raw.mp4'),
    [string]$FFmpeg = ''
)
. "$PSScriptRoot\Engine.ps1"
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EmberWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
}
'@
[EmberWin32]::SetProcessDPIAware() | Out-Null

if (-not $FFmpeg) {
    $FFmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
    if (-not $FFmpeg) { throw 'ffmpeg not found; pass -FFmpeg <path to ffmpeg.exe>.' }
}
$screen = [System.Windows.Forms.Screen]::AllScreens[$Monitor]
$bounds = $screen.Bounds
New-Item -ItemType Directory -Force -Path (Split-Path $Output) | Out-Null

$arguments = @($ProjectFile, '/Game/EmberLab/L_EmberLab', '-game', '-windowed', '-ResX=1920', '-ResY=1080',
    "-WinX=$($bounds.X + 100)", "-WinY=$($bounds.Y + 100)", '-NoVSync', '-NoSplash', '-nosound', '-EmberDemo',
    "-abslog=$([IO.Path]::ChangeExtension($Output, '.log'))")
$game = Start-Process -FilePath "$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList ($arguments | ForEach-Object { '"' + $_ + '"' }) -PassThru
while ($game.MainWindowHandle -eq 0 -and -not $game.HasExited) { Start-Sleep -Milliseconds 200; $game.Refresh() }
Start-Sleep -Seconds 2
$handle = $game.MainWindowHandle

# Centre the client area on the monitor, then measure where it actually landed.
$window = New-Object EmberWin32+RECT; $client = New-Object EmberWin32+RECT
[EmberWin32]::GetWindowRect($handle, [ref]$window) | Out-Null
[EmberWin32]::GetClientRect($handle, [ref]$client) | Out-Null
$origin = New-Object EmberWin32+POINT
[EmberWin32]::ClientToScreen($handle, [ref]$origin) | Out-Null
$frameLeft = $origin.X - $window.Left; $frameTop = $origin.Y - $window.Top
$targetX = $bounds.X + [int](($bounds.Width - $client.Right) / 2)
$targetY = $bounds.Y + [int](($bounds.Height - $client.Bottom) / 2)
[EmberWin32]::MoveWindow($handle, $targetX - $frameLeft, $targetY - $frameTop,
    $window.Right - $window.Left, $window.Bottom - $window.Top, $true) | Out-Null
Start-Sleep -Milliseconds 500
$origin = New-Object EmberWin32+POINT
[EmberWin32]::ClientToScreen($handle, [ref]$origin) | Out-Null
$width = $client.Right - $client.Left; $height = $client.Bottom - $client.Top
Write-Output "Capturing ${width}x${height} at ($($origin.X), $($origin.Y)) on monitor $Monitor $bounds"

$info = New-Object System.Diagnostics.ProcessStartInfo $FFmpeg
$info.Arguments = "-y -hide_banner -loglevel error -f gdigrab -framerate 60 -draw_mouse 0 -offset_x $($origin.X) -offset_y $($origin.Y) " +
    "-video_size ${width}x${height} -i desktop -c:v h264_nvenc -preset p5 -cq 20 -pix_fmt yuv420p `"$Output`""
$info.RedirectStandardInput = $true; $info.UseShellExecute = $false
$recorder = [System.Diagnostics.Process]::Start($info)
$started = Get-Date
$game.WaitForExit()
$recorder.StandardInput.Write('q'); $recorder.WaitForExit(15000) | Out-Null
Write-Output "Recorded $([int]((Get-Date) - $started).TotalSeconds) s to $Output"
