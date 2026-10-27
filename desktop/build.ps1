param([switch]$Test)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$csc = Join-Path $framework 'csc.exe'
$references = @(
    'System.dll', 'System.Core.dll', 'System.Xaml.dll', 'System.Web.Extensions.dll',
    (Join-Path $framework 'WPF\WindowsBase.dll'),
    (Join-Path $framework 'WPF\PresentationCore.dll'),
    (Join-Path $framework 'WPF\PresentationFramework.dll')
)
$arguments = @('/nologo', '/target:winexe', '/optimize+', '/utf8output',
    ('/out:' + (Join-Path $projectRoot 'WALL-E-Control.exe')),
    ('/resource:' + (Join-Path $PSScriptRoot 'Window.xaml') + ',WallE.Window.xaml'))
$arguments += $references | ForEach-Object { '/reference:' + $_ }
$arguments += (Join-Path $PSScriptRoot 'RobotConnection.cs'), (Join-Path $PSScriptRoot 'WallEControl.cs')
& $csc @arguments
if ($LASTEXITCODE -ne 0) { throw 'GUI compilation failed.' }
Write-Output 'Built WALL-E-Control.exe'
if ($Test) {
    $testRoot = Join-Path $projectRoot '.pio\gui-tests'
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
    $testArguments = @('/nologo', '/target:exe', '/utf8output',
        ('/out:' + (Join-Path $testRoot 'desktop_tests.exe')),
        ('/reference:' + (Join-Path $projectRoot 'WALL-E-Control.exe')))
    $testArguments += $references | ForEach-Object { '/reference:' + $_ }
    $testArguments += Join-Path $projectRoot 'tests\desktop_tests.cs'
    & $csc @testArguments
    if ($LASTEXITCODE -ne 0) { throw 'Desktop tests compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'WALL-E-Control.exe') -Destination (Join-Path $testRoot 'WALL-E-Control.exe') -Force
    $ErrorActionPreference = 'Continue'
    $testOutput = & (Join-Path $testRoot 'desktop_tests.exe') 2>&1
    $testExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $testOutput | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $testRoot 'results.txt')
    $testOutput | Write-Output
    if ($testExitCode -ne 0) { throw 'Desktop tests failed.' }
}
