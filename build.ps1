[CmdletBinding()]
param([string]$ZigPath = 'zig', [switch]$RunTests)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$zigCommand = Get-Command $ZigPath -ErrorAction Stop
$zigExe = $zigCommand.Source
$zigVersion = (& $zigExe version).Trim()
if ($LASTEXITCODE -ne 0 -or $zigVersion -ne '0.13.0') {
    throw 'This source snapshot is tested with Zig 0.13.0. Pass its zig.exe using -ZigPath.'
}
$outDir = Join-Path $repoRoot 'out'
$objectDir = Join-Path $outDir 'objects'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
$previousLocalCache = $env:ZIG_LOCAL_CACHE_DIR
$previousGlobalCache = $env:ZIG_GLOBAL_CACHE_DIR
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outDir 'cache-local'
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $outDir 'cache-global'
try {
    $minHook = Join-Path $repoRoot 'third_party\minhook'
    $objects = @()
    foreach ($unit in @('src\buffer.c','src\hook.c','src\trampoline.c','src\hde\hde64.c')) {
        $object = Join-Path $objectDir ((Split-Path $unit -Leaf) + '.o')
        & $zigExe cc -c -O2 -target x86_64-windows-gnu (Join-Path $minHook $unit) '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "MinHook compile failed: $unit" }
        $objects += $object
    }
    & $zigExe c++ -shared -O2 -std=c++17 -target x86_64-windows-gnu `
        (Join-Path $repoRoot 'src\uma_runtime.cpp') (Join-Path $repoRoot 'src\ear_motion.cpp') `
        @objects '-I' (Join-Path $minHook 'include') '-I' (Join-Path $repoRoot 'compat\eldenring_1_17') `
        '-o' (Join-Path $outDir 'uma_runtime.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Runtime compile failed' }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\uma-ears.ini') -Destination (Join-Path $outDir 'uma-ears.ini')
    Copy-Item -LiteralPath (Join-Path $minHook 'LICENSE.txt') -Destination (Join-Path $outDir 'MinHook-LICENSE.txt')
    if ($RunTests) {
        $testDir = Join-Path $outDir 'tests'
        New-Item -ItemType Directory -Path $testDir -Force | Out-Null
        foreach ($name in @('test_settings','test_transform','test_idle_stances','test_tail','test_tail_input','test_tail_idle','test_tail_fast_travel')) {
            $inputs = @((Join-Path $repoRoot "tests\$name.cpp"))
            if ($name -eq 'test_idle_stances') { $inputs += (Join-Path $repoRoot 'src\ear_motion.cpp') }
            $testExe = Join-Path $testDir "$name.exe"
            & $zigExe c++ -O2 -UNDEBUG -std=c++17 -target x86_64-windows-gnu @inputs '-I' (Join-Path $repoRoot 'src') '-o' $testExe
            if ($LASTEXITCODE -ne 0) { throw "Test compile failed: $name" }
            & $testExe
            if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
        }
    }
    Write-Output 'Built out/uma_runtime.dll. Build outputs are not part of the source repository.'
} finally {
    $env:ZIG_LOCAL_CACHE_DIR = $previousLocalCache
    $env:ZIG_GLOBAL_CACHE_DIR = $previousGlobalCache
}
