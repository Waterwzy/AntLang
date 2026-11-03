param(
    [switch]$Native,
    [int]$Fuzz = 40,
    [int]$NativeFuzz = 0,
    [switch]$Rebuild,
    [string]$CC = $env:ANT_CC
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Rebuild -or -not (Test-Path 'build\antc.exe')) {
    cmd /c build.bat
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$testArgs = @('tests\run_tests.py', '--fuzz', $Fuzz.ToString())
if ($Native) {
    $testArgs += '--native'
}
if ($NativeFuzz -gt 0) {
    $testArgs += @('--native-fuzz', $NativeFuzz.ToString())
}
if ($CC) {
    $testArgs += @('--cc', $CC)
}

python @testArgs
exit $LASTEXITCODE
