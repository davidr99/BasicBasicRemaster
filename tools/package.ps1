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
Copy-Item -LiteralPath (Join-Path $repo "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $repo "include") -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $repo "src") -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $repo "examples") -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $repo "docs") -Destination $stage -Recurse
New-Item -ItemType Directory -Path (Join-Path $stage "tools") | Out-Null
Copy-Item -LiteralPath (Join-Path $repo "tools\build_program.ps1") `
    -Destination (Join-Path $stage "tools")
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
    Copy-Item -LiteralPath $gccRoot -Destination (Join-Path $stage "toolchain") `
        -Recurse
}

$packageReadme = @"
BasicBasic $Version for 64-bit Windows

Double-click "BasicBasic IDE.cmd" or "bbasic-ide.exe". Open a .bas file, then
press F7 to compile or F5 to compile and run. Generated C and EXE files are
placed in build-ide-programs beside the IDE.

$(if ($WithoutToolchain) { "This compact package requires MinGW GCC on PATH or in BBASIC_GCC." } else { "This is the offline package. GCC is bundled under toolchain, so no compiler installation is required." })
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
