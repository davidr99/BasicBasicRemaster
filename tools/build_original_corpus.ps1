param(
    [string]$SourceDirectory = "K:\dos\bbasic",
    [string]$OutputDirectory,
    [string]$Gcc = "K:\msys64\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
$env:Path = (Split-Path -Parent $Gcc) + ";" + $env:Path
$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repo "build-original-corpus"
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$compiler = Join-Path $output "bbasicc.exe"
New-Item -ItemType Directory -Force -Path $output | Out-Null

& $Gcc -std=c11 -O2 -I (Join-Path $repo "include") `
    (Join-Path $repo "src\bbasicc.c") -o $compiler
if ($LASTEXITCODE -ne 0) { throw "Failed to build bbasicc." }

$programs = Get-ChildItem -LiteralPath $SourceDirectory -Filter "*.BAS" |
    Sort-Object Name
foreach ($program in $programs) {
    $generated = Join-Path $output ($program.BaseName + ".c")
    $executable = Join-Path $output ($program.BaseName + ".exe")
    & $compiler $program.FullName -o $generated
    if ($LASTEXITCODE -ne 0) { throw "Translation failed: $($program.Name)" }
    $subsystemOptions = @()
    if (Select-String -LiteralPath $generated `
            -SimpleMatch "BBASIC_SUBSYSTEM: WINDOWS" -Quiet) {
        $subsystemOptions += "-mwindows"
    }
    $linkInputs = @(
        $generated,
        (Join-Path $repo "src\bbasic_runtime.c"),
        (Join-Path $repo "src\bbasic_win32.c")
    )
    $iconMarker = Select-String -LiteralPath $generated `
        -Pattern 'BBASIC_ICON:\s*(?<path>.*?)\s*\*/' | Select-Object -First 1
    if ($iconMarker) {
        $iconName = $iconMarker.Matches[0].Groups['path'].Value.Trim()
        $iconPath = Join-Path $program.DirectoryName $iconName
        $iconFile = Get-Item -LiteralPath $iconPath -ErrorAction Stop
        $windres = Join-Path (Split-Path -Parent $Gcc) "windres.exe"
        if (-not (Test-Path -LiteralPath $windres -PathType Leaf)) {
            throw "The ICON directive requires windres.exe beside GCC: $windres"
        }
        $resourceScript = Join-Path $output ($program.BaseName + "-icon.rc")
        $resourceObject = Join-Path $output ($program.BaseName + "-icon.o")
        $resourceIcon = $iconFile.FullName.Replace('\', '/')
        Set-Content -LiteralPath $resourceScript -Encoding ASCII `
            -Value "1 ICON `"$resourceIcon`""
        & $windres -i $resourceScript -o $resourceObject
        if ($LASTEXITCODE -ne 0) {
            throw "Icon resource failed: $($program.Name)"
        }
        $linkInputs += $resourceObject
    }
    & $Gcc -std=c11 -O2 -I (Join-Path $repo "include") @linkInputs `
        @subsystemOptions -lm -lgdi32 -luser32 -lcomdlg32 -lwinmm -o $executable
    if ($LASTEXITCODE -ne 0) { throw "Link failed: $($program.Name)" }
    Write-Output "$($program.Name) -> $executable"
}

Write-Output "Built $($programs.Count) original BasicBasic programs."
