param(
    [string]$CanonicalRuntime = 'D:/works/Work-Ollama/freehold/LLM-Service/.local/duckdb-v1.5.5',
    [string]$BuildRoot = '',
    [ValidateRange(10,600)][int]$BuildTimeoutSeconds = 180,
    [switch]$IncludeSelectExpressions
)
$ErrorActionPreference = 'Stop'
$pgq = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$knowledge = [IO.Path]::GetFullPath((Join-Path $pgq '../../..'))
if (-not $BuildRoot) { $BuildRoot = Join-Path $knowledge '.b/pgqa' }
function Run-Bounded([string]$exe, [string[]]$arguments, [int]$seconds) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.WorkingDirectory = $knowledge
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($arg in $arguments) { $info.ArgumentList.Add($arg) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw "Cannot start $exe" }
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($seconds * 1000)) {
            $process.Kill($true)
            throw "Deadline exceeded: $exe"
        }
        Write-Output $out.GetAwaiter().GetResult()
        Write-Output $err.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Process failed: $exe, exit $($process.ExitCode)" }
    } finally { $process.Dispose() }
}
Run-Bounded 'cmake' @('-S', $PSScriptRoot, '-B', $BuildRoot, '-G', 'Visual Studio 18 2026', '-A', 'x64',
    '-DPGQ_CHECK_TRANSFORMERS=ON', "-DDUCKDB_INCLUDE=$pgq/duckdb/src/include",
    "-DDUCKDB_IMPORT_LIBRARY=$CanonicalRuntime/lib/duckdb.lib",
    "-DDUCKDB_STATIC_LIBRARY_DIR=$CanonicalRuntime/lib") $BuildTimeoutSeconds
Run-Bounded 'cmake' @('--build', $BuildRoot, '--config', 'Release', '--parallel', '2') $BuildTimeoutSeconds
Run-Bounded (Join-Path $BuildRoot 'Release/pgq_adapter_test.exe') @() 30
if ($IncludeSelectExpressions) {
    Run-Bounded 'cmake' @('--build', $BuildRoot, '--config', 'Release', '--target', 'pgq_select_expression_compile', '--parallel', '2') $BuildTimeoutSeconds
} else {
    Write-Output 'Partial baseline only: SELECT/Expression full-unit port gate NOT included (use -IncludeSelectExpressions).'
}
