# Invoked by Inno Setup as the original, non-elevated user.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InstallDir, [switch]$NoDependencies, [switch]$Uninstall)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$configDir = Join-Path $env:LOCALAPPDATA 'Barnino\BDK'
$configFile = Join-Path $configDir 'toolchain.json'
New-Item -ItemType Directory -Path $configDir -Force | Out-Null
$logFile = Join-Path $configDir 'setup.log'
Start-Transcript -Path $logFile -Append | Out-Null

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Update-ProcessPath {
    $env:PATH = [Environment]::GetEnvironmentVariable('PATH','Machine') + ';' +
                [Environment]::GetEnvironmentVariable('PATH','User')
}
function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return $candidate }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source -notlike '*\Microsoft\WindowsApps\python*') { return $command.Source }
    return $null
}
function Install-Dependency([string]$Id) {
    if ($NoDependencies) { throw "Missing $Id. Run setup without -NoDependencies." }
    if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        Write-Host 'Installing Windows Package Manager using Microsoft.WinGet.Client...'
        Install-PackageProvider -Name NuGet -MinimumVersion 2.8.5.201 -Scope CurrentUser -Force | Out-Null
        Install-Module -Name Microsoft.WinGet.Client -Repository PSGallery -Scope CurrentUser -Force -AllowClobber
        Import-Module Microsoft.WinGet.Client
        Repair-WinGetPackageManager -Latest
        Update-ProcessPath
    }
    Write-Host "Installing $Id (Windows may request administrator approval)..."
    Invoke-Checked 'winget.exe' @('install','--id',$Id,'--exact','--source','winget',
        '--architecture','x64','--silent','--accept-package-agreements','--accept-source-agreements','--disable-interactivity')
    Update-ProcessPath
}
function Find-DotNet {
    Find-Tool 'dotnet.exe' @((Join-Path $env:ProgramFiles 'dotnet\dotnet.exe'),
                             (Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'))
}
function Has-Sdk10([string]$DotNet) {
    if (-not $DotNet) { return $false }
    $sdks = & $DotNet --list-sdks
    return ($LASTEXITCODE -eq 0 -and [bool]($sdks -match '^10\.'))
}
try {
    $InstallDir = [IO.Path]::GetFullPath($InstallDir)
    $dotnet = Find-DotNet
    if ($Uninstall) {
        # Never remove compilers, shared runtimes, projects or another installation's settings.
        if (Test-Path -LiteralPath $configFile) {
            $config = Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json
            if ($config.InstallDir -eq $InstallDir) {
                if ($dotnet) {
                    $listing = & $dotnet new uninstall
                    if ($LASTEXITCODE -ne 0) { throw 'Could not read dotnet template registrations.' }
                    if ($listing -match 'Barnino.Systems.BDK.Templates') {
                        Invoke-Checked $dotnet @('new','uninstall','Barnino.Systems.BDK.Templates')
                    }
                }
                Remove-Item -LiteralPath $configFile
            }
        }
        exit 0
    }
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required.' }
    if (-not (Has-Sdk10 $dotnet)) { Install-Dependency 'Microsoft.DotNet.SDK.10'; $dotnet = Find-DotNet }
    if (-not (Has-Sdk10 $dotnet)) { throw '.NET SDK 10 is not available after installation. See setup.log.' }
    $pythonCandidates = @((Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
                           (Join-Path $env:ProgramFiles 'Python313\python.exe'))
    $python = Find-Tool 'python.exe' $pythonCandidates
    if ($python) { & $python -c 'import sys; sys.exit(0 if sys.version_info >= (3,9) else 1)'; if ($LASTEXITCODE -ne 0) { $python = $null } }
    if (-not $python) { Install-Dependency 'Python.Python.3.13'; $python = Find-Tool 'python.exe' $pythonCandidates }
    if (-not $python) { throw 'Python was not found after installation.' }
    Invoke-Checked $python @('-c','import sys; assert sys.version_info >= (3,9)')
    $llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
    $cc = Find-Tool 'clang.exe' @((Join-Path $llvm 'clang.exe'))
    $cxx = Find-Tool 'clang++.exe' @((Join-Path $llvm 'clang++.exe'))
    $ld = Find-Tool 'ld.lld.exe' @((Join-Path $llvm 'ld.lld.exe'), (Join-Path $llvm 'lld.exe'))
    if (-not $cc -or -not $cxx -or -not $ld) {
        Install-Dependency 'LLVM.LLVM'
        $cc = Find-Tool 'clang.exe' @((Join-Path $llvm 'clang.exe'))
        $cxx = Find-Tool 'clang++.exe' @((Join-Path $llvm 'clang++.exe'))
        $ld = Find-Tool 'ld.lld.exe' @((Join-Path $llvm 'ld.lld.exe'), (Join-Path $llvm 'lld.exe'))
    }
    if (-not $cc -or -not $cxx -or -not $ld) { throw 'LLVM clang/clang++/lld was not found after installation.' }
    $config = @{ InstallDir=$InstallDir; Python=$python; CC=$cc; CXX=$cxx; BDK_LD=$ld }
    $packages = @(Get-ChildItem -LiteralPath $InstallDir -Filter 'Barnino.Systems.BDK.Templates.*.nupkg')
    if ($packages.Count -ne 1) { throw 'The BDK template package is missing or ambiguous.' }
    $listing = & $dotnet new uninstall
    if ($LASTEXITCODE -ne 0) { throw 'Could not read dotnet template registrations.' }
    if ($listing -match 'Barnino.Systems.BDK.Templates') {
        Invoke-Checked $dotnet @('new','uninstall','Barnino.Systems.BDK.Templates')
    }
    Invoke-Checked $dotnet @('new','install',$packages[0].FullName)
    # Publish configuration only after tool discovery and template registration succeed.
    $config | ConvertTo-Json | Set-Content -LiteralPath $configFile -Encoding UTF8
    $env:BDK_PYTHON=$python; $env:CC=$cc; $env:CXX=$cxx; $env:BDK_LD=$ld
    $testRoot = Join-Path ([IO.Path]::GetTempPath()) ('BDK-' + [Guid]::NewGuid())
    try {
        foreach ($template in @('bca','bcawms','bca_cs','bcawms_cs','bga','bga_cs')) {
            $project = Join-Path $testRoot $template
            $arguments = @('new',$template,'-o',$project,'--no-update-check')
            if ($template -eq 'bcawms') { $arguments += '--c' }
            Invoke-Checked $dotnet $arguments
            Invoke-Checked $dotnet @('run','--project',$project)
            if (-not (Test-Path -LiteralPath (Join-Path $project 'elf\app.elf'))) { throw 'Smoke build produced no app.elf.' }
        }
    } finally { if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force } }
    Write-Host 'BDK is ready. Open a new terminal: dotnet new bca -o MyApp; cd MyApp; dotnet run'
} catch {
    Write-Host "BDK SETUP FAILED: $_" -ForegroundColor Red
    Write-Host "Log: $logFile"
    exit 1
} finally { Stop-Transcript | Out-Null }
