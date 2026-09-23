[CmdletBinding()]
param([string]$Iscc = (Join-Path $PSScriptRoot "build\tools\inno-6.7.3\ISCC.exe"))

$ErrorActionPreference = "Stop"
$testId = [guid]::NewGuid().ToString('N')
$appId = "QFTest.$testId"
$runValue = "QuotaFrameBridgeInstallerSmoke.$testId"
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$uninstallKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1"
$testRoot = Join-Path $PSScriptRoot "build\installer-smoke-$testId"
$installDir = Join-Path $testRoot 'installed'
$group = "QuotaFrame Installer Smoke $testId"
$sourceDir = Join-Path $PSScriptRoot 'dist'
$language = Join-Path $PSScriptRoot 'build\tools\ChineseSimplified.isl'
$runningApp = $null
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

function Run-Setup([string]$Path, [string[]]$Arguments, [bool]$ExpectFailure = $false) {
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) { throw "Installer did not exit within 60 seconds." }
    if ($ExpectFailure) {
        if ($process.ExitCode -eq 0) { throw "Installer ignored the shutdown veto." }
    } elseif ($process.ExitCode -ne 0) { throw "Installer failed with exit code $($process.ExitCode)." }
}

function Install-Version([string]$Version, [bool]$ExpectFailure = $false) {
    & $Iscc '/Q' "/DAppId=$appId" "/DRunValue=$runValue" "/DGroupName=$group" "/DAppVersion=$Version" "/DChineseMessages=$language" "/DSourceDir=$sourceDir" "/DOutputDir=$testRoot" (Join-Path $PSScriptRoot 'installer.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Smoke installer compilation failed.' }
    $setup = Join-Path $testRoot "quotaframe-bridge-windows-v$Version-setup.exe"
    Run-Setup $setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', ('/DIR="{0}"' -f $installDir), ('/GROUP="{0}"' -f $group), ('/LOG="{0}"' -f (Join-Path $testRoot "$Version-$ExpectFailure.log"))) $ExpectFailure
}

function Assert-NoAutostart {
    if ($null -ne (Get-ItemProperty -LiteralPath $runKey -Name $runValue -ErrorAction SilentlyContinue)) {
        throw 'The installer unexpectedly enabled autostart.'
    }
}

try {
    Install-Version '0.0.1'
    Assert-NoAutostart
    $installedExe = Join-Path $installDir 'quotaframe-bridge.exe'
    $expectedHash = (Get-FileHash (Join-Path $sourceDir 'quotaframe-bridge.exe')).Hash
    if ((Get-FileHash $installedExe).Hash -ne $expectedHash) { throw 'Installed executable mismatch.' }
    $preservedFile = Join-Path $installDir 'user-sentinel.txt'
    [IO.File]::WriteAllText($preservedFile, 'preserve this user file')
    Set-ItemProperty -LiteralPath $runKey -Name $runValue -Value '"C:\portable\quotaframe-bridge.exe" --autostarted'
    # Model a tray window that vetoes automatic shutdown until OTA completes.
    $fixtureSource = Join-Path $testRoot 'ShutdownFixture.cs'
    [IO.File]::WriteAllText($fixtureSource, @"
using System;
using System.IO;
using System.Windows.Forms;
class ShutdownFixture : NativeWindow {
    static string allowShutdown;
    protected override void WndProc(ref Message message) {
        if (message.Msg == 0x0011) {
            message.Result = File.Exists(allowShutdown) ? new IntPtr(1) : IntPtr.Zero;
            return;
        }
        if (message.Msg == 0x0016 || message.Msg == 0x0010) {
            if (File.Exists(allowShutdown) && (message.Msg == 0x0010 || message.WParam != IntPtr.Zero))
                Application.Exit();
            message.Result = IntPtr.Zero;
            return;
        }
        base.WndProc(ref message);
    }
    [STAThread] static void Main(string[] args) {
        allowShutdown = args[0] + ".allow";
        var window = new ShutdownFixture();
        window.CreateHandle(new CreateParams { Caption = "Installer shutdown fixture" });
        File.WriteAllText(args[0], "ready");
        Application.Run();
        GC.KeepAlive(window);
    }
}
"@)
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    & $compiler /nologo /target:winexe /reference:System.Windows.Forms.dll "/out:$installedExe" $fixtureSource
    if ($LASTEXITCODE -ne 0) { throw 'Shutdown fixture compilation failed.' }
    $readyFile = Join-Path $testRoot 'shutdown-fixture.ready'
    $runningApp = Start-Process -FilePath $installedExe -ArgumentList ('"{0}"' -f $readyFile) -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path -LiteralPath $readyFile)) {
        if ($runningApp.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Shutdown fixture did not become ready.' }
        Start-Sleep -Milliseconds 100
    }
    $fixtureHash = (Get-FileHash $installedExe).Hash
    Install-Version '0.0.2' $true
    if ($runningApp.HasExited) { throw 'OTA shutdown veto did not preserve the application.' }
    if ((Get-FileHash $installedExe).Hash -ne $fixtureHash) { throw 'Installer replaced a busy application.' }
    [IO.File]::WriteAllText($readyFile + '.allow', 'OTA finished')
    Install-Version '0.0.2'
    if (-not $runningApp.WaitForExit(5000)) { throw 'Upgrade did not close the running application.' }
    if ((Get-FileHash $installedExe).Hash -ne $expectedHash) { throw 'Upgrade did not replace the executable.' }
    if ((Get-ItemProperty -LiteralPath $uninstallKey).DisplayVersion -ne '0.0.2') { throw 'Upgrade did not retain its app identity.' }
    $expectedCommand = '"{0}" --autostarted' -f $installedExe
    if ((Get-ItemPropertyValue -LiteralPath $runKey -Name $runValue) -ne $expectedCommand) { throw 'Autostart did not follow the installation.' }
    Remove-ItemProperty -LiteralPath $runKey -Name $runValue
    Install-Version '0.0.3'
    Assert-NoAutostart
    $foreignCommand = '"C:\other\quotaframe-bridge.exe" --autostarted'
    Set-ItemProperty -LiteralPath $runKey -Name $runValue -Value $foreignCommand
    Run-Setup (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ((Get-ItemPropertyValue -LiteralPath $runKey -Name $runValue) -ne $foreignCommand) { throw 'Uninstall changed another installation autostart.' }
    Install-Version '0.0.4'
    if ((Get-ItemPropertyValue -LiteralPath $runKey -Name $runValue) -ne $expectedCommand) { throw 'Reinstallation did not migrate opt-in.' }
    Run-Setup (Join-Path $installDir 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    Assert-NoAutostart
    if (Test-Path -LiteralPath $installedExe) { throw 'Uninstall left the executable behind.' }
    if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstall left its registration behind.' }
    if ([IO.File]::ReadAllText($preservedFile) -ne 'preserve this user file') { throw 'User file was not preserved.' }
    Write-Host "PASS: install, OTA shutdown veto, running-application upgrade, opt-in migration, disabled preference, uninstall and user-file preservation. Logs: $testRoot"
}
finally {
    if ($null -ne $runningApp -and -not $runningApp.HasExited) {
        Stop-Process -Id $runningApp.Id -Force
        $runningApp.WaitForExit()
    }
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if ((Test-Path -LiteralPath $uninstallKey) -and (Test-Path -LiteralPath $uninstaller)) {
        Run-Setup $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    }
    Remove-ItemProperty -LiteralPath $runKey -Name $runValue -ErrorAction SilentlyContinue
}
