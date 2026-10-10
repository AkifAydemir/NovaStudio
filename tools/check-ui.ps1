$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    [xml]$theme = Get-Content -LiteralPath 'studio/App.xaml' -Raw
    [xml]$window = Get-Content -LiteralPath 'studio/MainWindow.xaml' -Raw
    $xamlNs = 'http://schemas.microsoft.com/winfx/2006/xaml'
    foreach ($style in $theme.SelectNodes("//*[local-name()='Style']")) {
        if ($style.TargetType -eq 'TextBlock' -and $style.GetAttribute('Key', $xamlNs)) {
            $hasForeground = $null -ne $style.SelectSingleNode("*[local-name()='Setter' and @Property='Foreground']")
            if (-not $hasForeground -and -not $style.GetAttribute('BasedOn')) { throw 'Named text style loses foreground inheritance.' }
        }
    }
    if ([double]$window.DocumentElement.Width -gt 1360 -or [double]$window.DocumentElement.Height -gt 780) { throw 'Default preview window exceeds tested desktop dimensions.' }
    if (-not $window.DocumentElement.GetAttribute('Foreground')) { throw 'Window must explicitly declare readable foreground.' }
    Write-Host 'XAML contrast/layout source contracts passed (not an interactive UI test).'
} finally { Pop-Location }
