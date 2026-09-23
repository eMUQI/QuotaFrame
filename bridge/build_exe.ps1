[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRepository,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$venvDir = Join-Path $root ".build-venv"
$lockPath = Join-Path $root "release-lock-windows.txt"
$requiredPythonVersion = "3.13.14"

$repositoryPattern = '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'
$repositorySegments = $ReleaseRepository.Split('/')
if (
    $ReleaseRepository -notmatch $repositoryPattern -or
    $repositorySegments[0] -in @('.', '..') -or
    $repositorySegments[1] -in @('.', '..')
) {
    throw "ReleaseRepository must be an owner/name repository."
}
if (-not (Test-Path $lockPath)) {
    throw "Release hash lock not found at '$lockPath'"
}

# The wheel hashes below were generated for CPython 3.13.14 on Windows x64.
# Reject another interpreter before creating the venv so a local release build
# cannot fail later with a misleading "no matching distribution" error.
$buildPythonVersion = (& $Python -c "import platform; print(platform.python_version())")
if ($LASTEXITCODE -ne 0) {
    throw "Failed to query the Python version from '$Python' (exit code $LASTEXITCODE)"
}
if ($buildPythonVersion.Trim() -ne $requiredPythonVersion) {
    throw "Release packaging requires Python $requiredPythonVersion because release-lock-windows.txt pins CPython 3.13 wheels; got $($buildPythonVersion.Trim()) from '$Python'. Pass -Python <path> to Python $requiredPythonVersion."
}

# Always rebuild the venv from scratch. External release dependencies are
# installed from a checked-in wheel/hash lock, so both versions and bytes are
# fixed for this platform instead of being selected again by the package index.
if (Test-Path $venvDir) {
    $resolvedVenv = (Resolve-Path -LiteralPath $venvDir).Path
    $expectedVenv = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".build-venv"))
    if ($resolvedVenv -ne $expectedVenv -or (Get-Item -LiteralPath $venvDir).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Build environment must be the project's .build-venv directory."
    }
    Remove-Item -LiteralPath $resolvedVenv -Recurse -Force
}

& $Python -m venv $venvDir
if ($LASTEXITCODE -ne 0) {
    throw "Failed to create virtual environment at '$venvDir' using '$Python' (exit code $LASTEXITCODE)"
}

$venvPython = Join-Path $venvDir "Scripts\python.exe"

& $venvPython -m pip install --quiet --only-binary=:all: --no-deps --require-hashes --requirement $lockPath
if ($LASTEXITCODE -ne 0) {
    throw "Hash-locked release dependency installation failed (exit code $LASTEXITCODE)"
}

# The local Bridge source is the checked-out release commit, not an index
# dependency. Install it without dependency resolution after all external
# packages have passed their hash checks.
& $venvPython -m pip install --quiet --no-deps --no-build-isolation "$root[build]"
if ($LASTEXITCODE -ne 0) {
    throw "Bridge source installation failed (exit code $LASTEXITCODE)"
}
& $venvPython -m pip check
if ($LASTEXITCODE -ne 0) {
    throw "Hash-locked release dependency set is inconsistent (exit code $LASTEXITCODE)"
}

$distPath = Join-Path $root "dist"
$workPath = Join-Path $root "build"
$srcPath = Join-Path $root "src"
$entryScript = Join-Path $srcPath "quotaframe_bridge\ui\app.py"
$assetPath = Join-Path (Split-Path $root -Parent) "assets\tray"
$iconPath = Join-Path $assetPath "quotaframe-bridge.ico"
$releaseChannelPath = Join-Path $workPath "release-channel.json"
$runtimePath = (& $venvPython -c "import sys; from pathlib import Path; print(Path(sys.base_prefix) / 'vcruntime140.dll')")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runtimePath)) {
    throw "The CPython VC runtime required by the managed CLI is missing."
}

if (-not (Test-Path $iconPath)) {
    throw "Tray icon not found at '$iconPath'"
}

New-Item -ItemType Directory -Path $workPath -Force | Out-Null
$releaseChannelJson = [pscustomobject]@{ repository = $ReleaseRepository } |
    ConvertTo-Json -Compress
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    $releaseChannelPath,
    $releaseChannelJson + "`n",
    $utf8NoBom
)

& $venvPython -m PyInstaller --onefile --windowed --name quotaframe-bridge --clean --noconfirm `
    --distpath $distPath --workpath $workPath --specpath $workPath `
    --icon $iconPath `
    --add-data "$assetPath;tray" `
    --add-data "$releaseChannelPath;quotaframe_bridge" `
    --add-binary "$runtimePath;." `
    -p $srcPath $entryScript

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller exited with code $LASTEXITCODE"
}

$exePath = Join-Path $distPath "quotaframe-bridge.exe"
if (-not (Test-Path $exePath)) {
    throw "Expected build output not found at '$exePath'"
}

$versionCheck = Start-Process -FilePath $exePath -ArgumentList "--version" -WindowStyle Hidden -Wait -PassThru
if ($versionCheck.ExitCode -ne 0) {
    throw "Frozen executable version check failed (exit code $($versionCheck.ExitCode))"
}

Write-Host "Built $exePath"
