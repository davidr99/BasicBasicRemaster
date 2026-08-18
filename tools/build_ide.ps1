param(
    [string]$Gcc = "K:\msys64\mingw64\bin\gcc.exe",
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$env:Path = (Split-Path -Parent $Gcc) + ";" + $env:Path
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repo "build-tools"
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null

$compiler = Join-Path $output "bbasicc.exe"
$editor = Join-Path $output "bbasic-ide.exe"

& $Gcc -std=c11 -O2 -Wall -Wextra -Wpedantic `
    (Join-Path $repo "src\bbasicc.c") -o $compiler
if ($LASTEXITCODE -ne 0) { throw "Failed to build the BasicBasic compiler." }

& $Gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -mwindows `
    (Join-Path $repo "src\bbasic_ide.c") `
    -lcomdlg32 -lgdi32 -luser32 -o $editor
if ($LASTEXITCODE -ne 0) { throw "Failed to build the BasicBasic IDE." }

Write-Output $editor
