param(
    [string]$Source,
    [string]$Screenshot,
    [string]$Ide,
    [string]$ExpectedGcc
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Ide) { $Ide = Join-Path $repo "build-tools\bbasic-ide.exe" }
if (-not $Source) { $Source = Join-Path $repo "examples\hello.bas" }
if (-not $Screenshot) { $Screenshot = Join-Path $repo "build-tools\bbasic-ide.png" }

if (-not (Test-Path -LiteralPath $Ide)) {
    throw "Build the IDE first with tools\build_ide.ps1."
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class BasicBasicIdeTestNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", CharSet = CharSet.Ansi)]
    public static extern IntPtr FindWindow(string className, string title);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr parent, int identifier);

    [DllImport("user32.dll", CharSet = CharSet.Ansi)]
    public static extern int GetWindowText(IntPtr window, StringBuilder text,
                                           int capacity);

    [DllImport("user32.dll")]
    public static extern int GetWindowTextLength(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr window, uint message,
                                            IntPtr wparam, IntPtr lparam);

    [DllImport("user32.dll", CharSet = CharSet.Ansi,
               EntryPoint = "SendMessageA")]
    public static extern IntPtr SendMessageText(IntPtr window, uint message,
                                                IntPtr wparam,
                                                StringBuilder lparam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PostMessage(IntPtr window, uint message,
                                          IntPtr wparam, IntPtr lparam);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext,
                                          uint flags);
}
'@

function Get-ControlText([IntPtr]$Control) {
    $length = [int][BasicBasicIdeTestNative]::SendMessage(
        $Control, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero)
    $text = [Text.StringBuilder]::new($length + 1)
    [void][BasicBasicIdeTestNative]::SendMessageText(
        $Control, 0x000D, [IntPtr]$text.Capacity, $text)
    return $text.ToString()
}

$start = [Diagnostics.ProcessStartInfo]::new((Resolve-Path -LiteralPath $Ide).Path)
$start.UseShellExecute = $false
$resolvedSource = (Resolve-Path -LiteralPath $Source).Path
$start.Arguments = '"' + $resolvedSource + '"'
$process = [Diagnostics.Process]::Start($start)

try {
    $window = [IntPtr]::Zero
    $sourceEditor = [IntPtr]::Zero
    $outputEditor = [IntPtr]::Zero
    $compileButton = [IntPtr]::Zero
    $runButton = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        $window = $process.MainWindowHandle
        if ($window -ne [IntPtr]::Zero) {
            $sourceEditor = [BasicBasicIdeTestNative]::GetDlgItem($window, 500)
            $outputEditor = [BasicBasicIdeTestNative]::GetDlgItem($window, 501)
            $compileButton = [BasicBasicIdeTestNative]::GetDlgItem($window, 300)
            $runButton = [BasicBasicIdeTestNative]::GetDlgItem($window, 301)
            if ($sourceEditor -ne [IntPtr]::Zero -and
                $outputEditor -ne [IntPtr]::Zero -and
                $compileButton -ne [IntPtr]::Zero -and
                $runButton -ne [IntPtr]::Zero) { break }
        }
        Start-Sleep -Milliseconds 100
    }
    if ($sourceEditor -eq [IntPtr]::Zero) {
        throw "The IDE did not finish creating its controls."
    }
    if ((Get-ControlText $sourceEditor) -notmatch
        "Hello from modern BasicBasic") {
        throw "The command-line source file was not loaded."
    }

    [void][BasicBasicIdeTestNative]::SendMessage(
        $compileButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $buildOutput = Get-ControlText $outputEditor
    } while ($buildOutput -notmatch "Build (succeeded|failed)" -and
             [DateTime]::UtcNow -lt $deadline)
    if ($buildOutput -notmatch "Build succeeded") {
        throw "IDE compile failed or timed out:`n$buildOutput"
    }
    if ($ExpectedGcc) {
        $resolvedGcc = (Resolve-Path -LiteralPath $ExpectedGcc).Path
        if (-not $buildOutput.Contains($resolvedGcc)) {
            throw "The IDE did not use the packaged GCC:`n$buildOutput"
        }
    }

    $rectangle = [BasicBasicIdeTestNative+RECT]::new()
    if (-not [BasicBasicIdeTestNative]::GetWindowRect($window, [ref]$rectangle)) {
        throw "Could not read the IDE window rectangle."
    }
    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    $bitmap = [Drawing.Bitmap]::new($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try {
        if (-not [BasicBasicIdeTestNative]::PrintWindow($window, $dc, 2)) {
            throw "Could not capture the IDE window."
        }
    } finally {
        $graphics.ReleaseHdc($dc)
        $graphics.Dispose()
    }
    $bitmap.Save($Screenshot, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()

    [void][BasicBasicIdeTestNative]::SendMessage(
        $runButton, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $runOutput = Get-ControlText $outputEditor
    } while ($runOutput -notmatch "Program started" -and
             $runOutput -notmatch "Build failed" -and
             [DateTime]::UtcNow -lt $deadline)
    if ($runOutput -notmatch "Program started") {
        throw "IDE run failed or timed out:`n$runOutput"
    }

    Write-Host "IDE open, compile, and run smoke test passed."
    Write-Host "Screenshot: $Screenshot"
} finally {
    if ($window -ne [IntPtr]::Zero) {
        [void][BasicBasicIdeTestNative]::PostMessage(
            $window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    }
    if (-not $process.HasExited) {
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
