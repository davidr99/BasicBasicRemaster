param(
    [string]$Gcc = "K:\msys64\mingw64\bin\gcc.exe",
    [string]$OutputDirectory,
    [string]$Version = "0.1.0",
    [switch]$WithoutToolchain
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repo "dist"
}
$dist = [IO.Path]::GetFullPath($OutputDirectory)
$packageName = "BasicBasic-$Version-win64"
$stage = Join-Path $dist $packageName
$archive = Join-Path $dist ($packageName + ".zip")
$checksum = $archive + ".sha256"

if (-not (Test-Path -LiteralPath $Gcc -PathType Leaf)) {
    throw "GCC was not found at $Gcc"
}
$gccBin = Split-Path -Parent (Resolve-Path -LiteralPath $Gcc).Path
$gccRoot = Split-Path -Parent $gccBin

New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
if (Test-Path -LiteralPath $checksum) {
    Remove-Item -LiteralPath $checksum -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null

$build = Join-Path $dist "package-build"
& (Join-Path $PSScriptRoot "build_ide.ps1") -Gcc $Gcc `
    -OutputDirectory $build | Out-Null

Copy-Item -LiteralPath (Join-Path $build "bbasic-ide.exe") -Destination $stage
Copy-Item -LiteralPath (Join-Path $build "bbasicc.exe") -Destination $stage
$include = Join-Path $stage "include"
$source = Join-Path $stage "src"
New-Item -ItemType Directory -Path $include | Out-Null
New-Item -ItemType Directory -Path $source | Out-Null
Copy-Item -LiteralPath (Join-Path $repo "include\bbasic_runtime.h") `
    -Destination $include
Copy-Item -LiteralPath (Join-Path $repo "src\bbasic_runtime.c") `
    -Destination $source
Copy-Item -LiteralPath (Join-Path $repo "src\bbasic_win32.c") `
    -Destination $source
Set-Content -LiteralPath (Join-Path $stage "VERSION.txt") `
    -Value $Version -Encoding ASCII

$launcher = @'
@echo off
start "BasicBasic IDE" "%~dp0bbasic-ide.exe" %*
'@
Set-Content -LiteralPath (Join-Path $stage "BasicBasic IDE.cmd") `
    -Value $launcher -Encoding ASCII

if (-not $WithoutToolchain) {
    Write-Host "Copying the portable GCC toolchain..."
    $toolchain = Join-Path $stage "toolchain"
    New-Item -ItemType Directory -Path $toolchain | Out-Null
    $robocopy = Join-Path $env:SystemRoot "System32\robocopy.exe"
    if (-not (Test-Path -LiteralPath $robocopy)) {
        throw "Windows robocopy.exe is required to stage the GCC toolchain."
    }
    & $robocopy $gccRoot $toolchain /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 `
        /NFL /NDL /NJH /NJS /NP
    if ($LASTEXITCODE -ge 8) {
        throw "Failed to copy the GCC toolchain (robocopy exit $LASTEXITCODE)."
    }
}

$packageReadme = @"
BasicBasic $Version for 64-bit Windows

Double-click "BasicBasic IDE.cmd" or "bbasic-ide.exe". Open a .bas file, then
press F7 to compile or F5 to compile and run. Generated C and EXE files are
placed in build-ide-programs beside the IDE.

$(if ($WithoutToolchain) { "This compact package requires MinGW GCC on PATH or in BBASIC_GCC." } else { "This is the offline package. GCC is bundled under toolchain, so no compiler installation is required." })

This end-user package contains only the application and its compilation/runtime
dependencies. Project documentation, tests, and sample sources remain on GitHub.
"@
Set-Content -LiteralPath (Join-Path $stage "START HERE.txt") `
    -Value $packageReadme -Encoding UTF8

Write-Host "Creating $archive ..."
$windowsTar = Join-Path $env:SystemRoot "System32\tar.exe"
if (-not (Test-Path -LiteralPath $windowsTar)) {
    throw "Windows tar.exe is required to create the deployment archive."
}
& $windowsTar -a -cf $archive -C $dist $packageName
if ($LASTEXITCODE -ne 0) { throw "Failed to create the deployment archive." }
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $checksum `
    -Value "$hash  $($packageName).zip" -Encoding ASCII

Write-Output ([pscustomobject]@{
    PackageDirectory = $stage
    Archive = $archive
    Checksum = $checksum
    IncludesToolchain = -not $WithoutToolchain
})
