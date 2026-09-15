param(
    [ValidateSet('pi','calls','strings')][string]$Workload = 'pi',
    [ValidateRange(1,100000)][int]$Samples = 200,
    [string]$Executable = 'build/cmake/Release/cifa_benchmark.exe',
    [string]$Output = 'build/results/vm.diagsession',
    [switch]$Pool
)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$collector = Join-Path $vs 'Team Tools\DiagnosticsHub\Collector\VSDiagnostics.exe'
$config = Join-Path (Split-Path $collector) 'AgentConfigs\CpuUsageBase.json'
$exe = (Resolve-Path $Executable).Path
$outputPath = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force (Split-Path $outputPath) | Out-Null
# Target only our benchmark. Use the installed Visual Studio collector directly.
$benchmarkArgs = "$Samples $Workload --vm-only --wait"
if($Pool) { $benchmarkArgs += ' --pool' } else { $benchmarkArgs += ' --no-pool' }
$process = Start-Process $exe -ArgumentList $benchmarkArgs -PassThru -WindowStyle Hidden
$session = 42
$started = $false
try {
    & $collector start $session "/attach:$($process.Id)" "/loadConfig:$config"
    if ($LASTEXITCODE) { throw 'Visual Studio CPU collector failed' }
    $started = $true
    $process.WaitForExit()
    if ($process.ExitCode) { throw "Benchmark failed: $($process.ExitCode)" }
} finally {
    if ($started) {
        & $collector stop $session "/output:$outputPath"
        if ($LASTEXITCODE) { throw 'Saving the profile failed' }
    }
    if (!$process.HasExited) { $process.Kill() }
}
Write-Host "Open in Visual Studio: $outputPath"
