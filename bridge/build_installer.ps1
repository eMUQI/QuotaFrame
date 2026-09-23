[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$Iscc = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$dist = Join-Path $root "dist"
$exe = Join-Path $dist "quotaframe-bridge.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build quotaframe-bridge.exe with build_exe.ps1 first."
}
$version = & $Python -c "import sys; sys.path.insert(0, sys.argv[1]); from quotaframe_bridge import __version__; from quotaframe_bridge.versioning import SemVer; print(SemVer.parse(__version__))" (Join-Path $root "src")
if ($LASTEXITCODE -ne 0) { throw "Cannot determine Bridge version." }

$toolsDir = Join-Path $root "build\tools"
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
$chineseMessages = Join-Path $toolsDir "ChineseSimplified.isl"
$languageHash = "7d544b9bb1d142cfa11f2e5d3cc8abe2e55f8e066c5124e3772675aa236e1278"
if (-not (Test-Path -LiteralPath $chineseMessages) -or (Get-FileHash -LiteralPath $chineseMessages).Hash -ne $languageHash) {
    Invoke-WebRequest "https://raw.githubusercontent.com/jrsoftware/issrc/4adf37ed7f3fd2bd11c6836ba056e3de170fbabf/Files/Languages/Unofficial/ChineseSimplified.isl" -OutFile $chineseMessages
}
if ((Get-FileHash -LiteralPath $chineseMessages).Hash -ne $languageHash) {
    throw "Inno Setup Chinese language checksum mismatch."
}

if (-not $Iscc) {
    $compilerDir = Join-Path $toolsDir "inno-6.7.3"
    $Iscc = Join-Path $compilerDir "ISCC.exe"
    if (-not (Test-Path -LiteralPath $Iscc)) {
        $package = Join-Path $toolsDir "innosetup-6.7.3.exe"
        $sha256 = "9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732"
        if (-not (Test-Path -LiteralPath $package) -or (Get-FileHash -LiteralPath $package).Hash -ne $sha256) {
            Invoke-WebRequest "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe" -OutFile $package
        }
        if ((Get-FileHash -LiteralPath $package).Hash -ne $sha256) {
            throw "Inno Setup package checksum mismatch."
        }
        $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/PORTABLE=1', ('/DIR="{0}"' -f $compilerDir))
        $process = Start-Process -FilePath $package -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "Inno Setup portable extraction failed." }
    }
}
if (-not (Test-Path -LiteralPath $Iscc)) { throw "Inno Setup compiler was not found." }
& $Iscc "/DChineseMessages=$chineseMessages" "/DAppVersion=$version" "/DSourceDir=$dist" "/DOutputDir=$dist" (Join-Path $root "installer.iss")
if ($LASTEXITCODE -ne 0) { throw "Windows installer compilation failed." }
$installer = Join-Path $dist "quotaframe-bridge-windows-v$version-setup.exe"
if (-not (Test-Path -LiteralPath $installer)) { throw "Windows installer output is missing." }
Write-Host "Built $installer"
