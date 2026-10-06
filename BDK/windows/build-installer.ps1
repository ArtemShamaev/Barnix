[CmdletBinding()]
param([string]$Compiler)
$ErrorActionPreference = 'Stop'
if (-not $Compiler) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $Compiler = $command.Source }
    else {
        foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, $env:LOCALAPPDATA)) {
            if (-not $root) { continue }
            foreach ($relative in @('Inno Setup 6\ISCC.exe','Inno Setup 7\ISCC.exe','Programs\Inno Setup 6\ISCC.exe')) {
                $candidate = Join-Path $root $relative
                if (Test-Path -LiteralPath $candidate) { $Compiler = $candidate; break }
            }
            if ($Compiler) { break }
        }
    }
}
if (-not $Compiler) { throw 'Install Inno Setup 6.7+ or pass -Compiler C:\path\ISCC.exe' }
& $Compiler (Join-Path $PSScriptRoot 'bdk.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed ($LASTEXITCODE)" }
