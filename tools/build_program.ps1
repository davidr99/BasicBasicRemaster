param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$InputBas,

    [Parameter(Position = 1)]
    [string]$OutputExe,

    [string]$Gcc = "K:\msys64\mingw64\bin\gcc.exe",

    [switch]$WindowsApp,

    [switch]$ConsoleApp
)

$ErrorActionPreference = "Stop"
if ($WindowsApp -and $ConsoleApp) {
    throw "Choose either -WindowsApp or -ConsoleApp, not both."
}
$env:Path = (Split-Path -Parent $Gcc) + ";" + $env:Path
$gccRoot = Split-Path -Parent (Split-Path -Parent $Gcc)
$gccSystemInclude = Join-Path $gccRoot "include"
$gccSystemOptions = @()
if (Test-Path -LiteralPath (Join-Path $gccSystemInclude "stddef.h")) {
    $gccSystemOptions += "-isystem", $gccSystemInclude
}
$repo = Split-Path -Parent $PSScriptRoot
$inputFile = Get-Item -LiteralPath $InputBas
if ([string]::IsNullOrWhiteSpace($OutputExe)) {
    $OutputExe = Join-Path $inputFile.DirectoryName ($inputFile.BaseName + ".exe")
}
$outputPath = [System.IO.Path]::GetFullPath($OutputExe)
$buildDirectory = Join-Path $repo "build-tools"
$compiler = Join-Path $buildDirectory "bbasicc.exe"
$generated = Join-Path $buildDirectory ($inputFile.BaseName + ".c")

New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

& $Gcc -std=c11 -O2 @gccSystemOptions -I (Join-Path $repo "include") `
    (Join-Path $repo "src\bbasicc.c") -o $compiler
if ($LASTEXITCODE -ne 0) { throw "Failed to build the BasicBasic compiler." }

& $compiler $inputFile.FullName -o $generated
if ($LASTEXITCODE -ne 0) { throw "BasicBasic translation failed." }

$generatedWindowsApp = Select-String -LiteralPath $generated `
    -SimpleMatch "BBASIC_SUBSYSTEM: WINDOWS" -Quiet
$subsystemOptions = @()
if ($WindowsApp -or (-not $ConsoleApp -and $generatedWindowsApp)) {
    $subsystemOptions += "-mwindows"
}

& $Gcc -std=c11 -O2 @gccSystemOptions -I (Join-Path $repo "include") $generated `
    (Join-Path $repo "src\bbasic_runtime.c") `
    (Join-Path $repo "src\bbasic_win32.c") `
    @subsystemOptions -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm -o $outputPath
if ($LASTEXITCODE -ne 0) { throw "Failed to link the generated Windows program." }

Write-Output $outputPath
