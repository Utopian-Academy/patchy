param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$patchyRoot = Split-Path -Parent $PSScriptRoot
Push-Location $patchyRoot
try {
    cmake -S . -B build -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    cmake --build build --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
} finally {
    Pop-Location
}
