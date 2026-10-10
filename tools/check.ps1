$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    & (Join-Path $PSScriptRoot 'check-ui.ps1')
    function Invoke-Checked {
        param([string]$Program, [string[]]$Arguments)
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
    }
    Invoke-Checked -Program cmake -Arguments @('-S', '.', '-B', 'build', '-A', 'x64')
    Invoke-Checked -Program cmake -Arguments @('--build', 'build', '--config', 'Release')
    Invoke-Checked -Program ctest -Arguments @('--test-dir', 'build', '-C', 'Release', '--output-on-failure')
    Invoke-Checked -Program dotnet -Arguments @('build', 'studio/NovaStudio.csproj', '-c', 'Release')
    Write-Host 'Build and automated checks completed. Interactive UI is a separate check.'
} finally {
    Pop-Location
}
