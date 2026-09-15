param(
    [string]$Baseline = 'build/baseline/cifa_benchmark.exe',
    [string]$Candidate = 'build/cmake/Release/cifa_benchmark.exe',
    [ValidateRange(1,100000)][int]$Samples = 15,
    [string]$OutputDirectory = 'build/results/comparison'
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$batch = 0
foreach ($variant in @('A','B','B','A','A','B')) {
    ++$batch
    $exe = if ($variant -eq 'A') { $Baseline } else { $Candidate }
    foreach ($workload in @('pi','calls')) {
        & $exe $Samples $workload | Tee-Object (Join-Path $OutputDirectory "$batch-$variant-$workload.txt")
        if ($LASTEXITCODE) { throw "Benchmark failed: $variant $workload" }
    }
}
