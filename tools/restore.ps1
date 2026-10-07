# Downloads nuget.exe (if missing) and restores the packages listed in src/packages.config.
$root = Split-Path $PSScriptRoot -Parent
$nuget = Join-Path $PSScriptRoot "nuget.exe"
if (-not (Test-Path $nuget)) {
    Invoke-WebRequest "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $nuget -UseBasicParsing
}
& $nuget restore (Join-Path $root "src\packages.config") -PackagesDirectory (Join-Path $root "packages") -NonInteractive
