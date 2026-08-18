param(
    [string]$Gcc = "K:\msys64\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$testDirectory = Join-Path $repo "build-tools"
New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null

$cases = @("hello", "core", "core_extended", "file_io", "win32_smoke")
foreach ($case in $cases) {
    & (Join-Path $PSScriptRoot "build_program.ps1") `
        (Join-Path $repo "examples\$case.bas") `
        (Join-Path $testDirectory "$case.exe") -Gcc $Gcc | Out-Null
}

& $Gcc -std=c11 -O2 -mwindows -I (Join-Path $repo "include") `
    (Join-Path $repo "tests\win32_input_smoke.c") `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm `
    -o (Join-Path $testDirectory "win32_input_smoke.exe")
if ($LASTEXITCODE -ne 0) { throw "Failed to build the Win32 input smoke test." }

& $Gcc -std=c11 -O2 -mwindows -I (Join-Path $repo "include") `
    (Join-Path $repo "tests\win32_dialog_smoke.c") `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm `
    -o (Join-Path $testDirectory "win32_dialog_smoke.exe")
if ($LASTEXITCODE -ne 0) { throw "Failed to build the Win32 dialog smoke test." }

& $Gcc -std=c11 -O2 -mwindows -I (Join-Path $repo "include") `
    (Join-Path $repo "tests\win32_pie_smoke.c") `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm `
    -o (Join-Path $testDirectory "win32_pie_smoke.exe")
if ($LASTEXITCODE -ne 0) { throw "Failed to build the Win32 pie smoke test." }

& $Gcc -std=c11 -O2 -mwindows -I (Join-Path $repo "include") `
    (Join-Path $repo "tests\win32_controls_smoke.c") `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm `
    -o (Join-Path $testDirectory "win32_controls_smoke.exe")
if ($LASTEXITCODE -ne 0) { throw "Failed to build the Win32 controls smoke test." }

& $Gcc -std=c11 -O2 -I (Join-Path $repo "include") `
    (Join-Path $repo "tests\serial_fallback_smoke.c") `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm `
    -o (Join-Path $testDirectory "serial_fallback_smoke.exe")
if ($LASTEXITCODE -ne 0) { throw "Failed to build the serial fallback smoke test." }

$hello = (& (Join-Path $testDirectory "hello.exe") | Out-String)
if ($hello -notmatch "Hello from modern BasicBasic!") {
    throw "Hello regression failed."
}

$core = (& (Join-Path $testDirectory "core.exe") | Out-String)
if ($core -notmatch "branch two" -or $core -notmatch "4") {
    throw "Core-language regression failed."
}

$extended = (& (Join-Path $testDirectory "core_extended.exe") | Out-String)
if ($extended -notmatch "TOTAL:12" -or $extended -notmatch "2\s+0\s+59" -or
    $extended -notmatch "INLINEIF:\s*7") {
    throw "Extended-language or embedded-NUL regression failed."
}

Push-Location $testDirectory
try {
    $fileOutput = (& .\file_io.exe | Out-String)
} finally {
    Pop-Location
}
if ($fileOutput -notmatch "7" -or $fileOutput -notmatch "hello" -or
    $fileOutput -notmatch "AB\s+CD") {
    throw "File-I/O regression failed."
}

$marker = Join-Path $testDirectory "win32-smoke.ok"
Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
Push-Location $testDirectory
try {
    $win32Process = Start-Process -FilePath ".\win32_smoke.exe" `
        -Wait -PassThru -WindowStyle Hidden
    if ($win32Process.ExitCode -ne 0) { throw "Win32 smoke executable failed." }
} finally {
    Pop-Location
}
if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne "PASS") {
    throw "Win32 smoke marker was not produced."
}

$inputMarker = Join-Path $testDirectory "win32-input-smoke.ok"
Remove-Item -LiteralPath $inputMarker -Force -ErrorAction SilentlyContinue
Push-Location $testDirectory
try {
    $inputProcess = Start-Process -FilePath ".\win32_input_smoke.exe" `
        -PassThru -WindowStyle Hidden
    if (-not $inputProcess.WaitForExit(5000)) {
        $inputProcess.Kill()
        throw "Win32 GUI input smoke test timed out."
    }
    if ($inputProcess.ExitCode -ne 0) {
        throw "Win32 GUI input smoke executable failed with exit code $($inputProcess.ExitCode)."
    }
} finally {
    Pop-Location
}
if ((Get-Content -LiteralPath $inputMarker -Raw).Trim() -ne "PASS") {
    throw "Win32 GUI input smoke marker was not produced."
}

$dialogMarker = Join-Path $testDirectory "win32-dialog-smoke.ok"
Remove-Item -LiteralPath $dialogMarker -Force -ErrorAction SilentlyContinue
Push-Location $testDirectory
try {
    $dialogProcess = Start-Process -FilePath ".\win32_dialog_smoke.exe" `
        -PassThru -WindowStyle Hidden
    if (-not $dialogProcess.WaitForExit(5000)) {
        $dialogProcess.Kill()
        throw "Win32 dialog smoke test timed out."
    }
    if ($dialogProcess.ExitCode -ne 0) {
        throw "Win32 dialog smoke executable failed with exit code $($dialogProcess.ExitCode)."
    }
} finally {
    Pop-Location
}
if ((Get-Content -LiteralPath $dialogMarker -Raw).Trim() -ne "PASS") {
    throw "Win32 dialog smoke marker was not produced."
}

$pieMarker = Join-Path $testDirectory "win32-pie-smoke.ok"
Remove-Item -LiteralPath $pieMarker -Force -ErrorAction SilentlyContinue
Push-Location $testDirectory
try {
    $pieProcess = Start-Process -FilePath ".\win32_pie_smoke.exe" `
        -PassThru -WindowStyle Hidden
    if (-not $pieProcess.WaitForExit(5000)) {
        $pieProcess.Kill()
        throw "Win32 pie smoke test timed out."
    }
    if ($pieProcess.ExitCode -ne 0) {
        throw "Win32 pie smoke executable failed with exit code $($pieProcess.ExitCode)."
    }
} finally {
    Pop-Location
}
if ((Get-Content -LiteralPath $pieMarker -Raw).Trim() -ne "PASS") {
    throw "Win32 pie smoke marker was not produced."
}

$controlsMarker = Join-Path $testDirectory "win32-controls-smoke.ok"
Remove-Item -LiteralPath $controlsMarker -Force -ErrorAction SilentlyContinue
Push-Location $testDirectory
try {
    $controlsProcess = Start-Process -FilePath ".\win32_controls_smoke.exe" `
        -PassThru -WindowStyle Hidden
    if (-not $controlsProcess.WaitForExit(5000)) {
        $controlsProcess.Kill()
        throw "Win32 controls smoke test timed out."
    }
    if ($controlsProcess.ExitCode -ne 0) {
        throw "Win32 controls smoke executable failed with exit code $($controlsProcess.ExitCode)."
    }
} finally {
    Pop-Location
}
if ((Get-Content -LiteralPath $controlsMarker -Raw).Trim() -ne "PASS") {
    throw "Win32 controls smoke marker was not produced."
}

$previousVirtualSerial = $env:BBASIC_FORCE_VIRTUAL_SERIAL
try {
    $env:BBASIC_FORCE_VIRTUAL_SERIAL = "1"
    & (Join-Path $testDirectory "serial_fallback_smoke.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "Virtual serial fallback smoke test failed."
    }
} finally {
    $env:BBASIC_FORCE_VIRTUAL_SERIAL = $previousVirtualSerial
}

Write-Output "All modern BasicBasic regressions passed."
