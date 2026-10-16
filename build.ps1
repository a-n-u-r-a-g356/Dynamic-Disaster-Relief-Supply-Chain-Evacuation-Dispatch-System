# Clean build for Windows PowerShell + MSYS2 GCC/G++.
# Run from anywhere:   .\build.ps1            (build only)
#                      .\build.ps1 -Run       (build, then start the program)
#                      .\build.ps1 -Test      (build, then run the tests)
# It stops at the first compile/link error and never runs an old executable.
param([switch]$Run, [switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

function Run-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "FAILED: $exe $($arguments -join ' ')" }
}

# A stale relief.exe in the project root was the likely cause of the old 'python3 not found' error.
if (Test-Path .\relief.exe) { Remove-Item .\relief.exe -Force; Write-Host "Removed stale .\relief.exe (the real one is build\relief.exe)" }
if (Test-Path .\build) { Remove-Item .\build -Recurse -Force }
New-Item -ItemType Directory -Path .\build\obj | Out-Null

$objs = @()
foreach ($f in Get-ChildItem .\src\c\*.c) {
    $o = ".\build\obj\$($f.BaseName).o"
    Write-Host "gcc  $($f.Name)"
    Run-Native gcc @('-std=c11', '-Wall', '-Wextra', '-Iinclude', '-c', $f.FullName, '-o', $o)
    $objs += $o
}
foreach ($f in Get-ChildItem .\src\*.cpp) {
    $o = ".\build\obj\$($f.BaseName).o"
    Write-Host "g++  $($f.Name)"
    Run-Native g++ @('-std=c++17', '-Wall', '-Wextra', '-Iinclude', '-Isrc', '-c', $f.FullName, '-o', $o)
    $objs += $o
}
Write-Host "link build\relief.exe"
Run-Native g++ (@('-o', '.\build\relief.exe') + $objs)
Write-Host "BUILD OK -> build\relief.exe" -ForegroundColor Green

if ($Test) {
    $c = @('graph', 'hashmap', 'heap', 'queue') | ForEach-Object { ".\build\obj\$_.o" }
    Run-Native gcc (@('-std=c11', '-Wall', '-Iinclude', '.\tests\test_dsa.c') + $c + @('-o', '.\build\test_dsa.exe'))
    Run-Native .\build\test_dsa.exe @()
    Run-Native .\build\relief.exe @('--script', 'tests\session_robust.txt')
    Run-Native py @('tests\test_fetcher.py')
}
if ($Run) { & .\build\relief.exe }
