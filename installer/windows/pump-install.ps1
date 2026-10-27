# pump: installer / uninstaller for Windows (run by "Install pump.bat" and "Uninstall pump.bat").
#
# Install: every pump plugin already installed (pump-*.vst3 and pump-*.clap, in the system plug-in folders and the
# per-user ones) is moved into one backup folder, pump-previous (the last version only; outside the plug-in folders,
# so hosts don't find it), then the plugins in this folder are copied in. Plugins that were renamed or removed in the
# new version go with the old ones, so nothing is left twice. If a host has a pump plugin open, its files can't be
# replaced: nothing is changed and it says to close the host first.
# Uninstall: the same, without the copying in, and the backup goes too.
#
# Written for Windows PowerShell 5.1 (every Windows 10 / 11 has it). PUMP_TEST_ROOT=<dir> runs it against folders in
# <dir> instead of the real ones, without asking for administrator rights (tests).
param([switch]$Uninstall, [string]$From = "")

$ErrorActionPreference = 'Stop'
$script = $MyInvocation.MyCommand.Path
if (-not $From) { $From = Split-Path -Parent (Split-Path -Parent $script) }
$From = [System.IO.Path]::GetFullPath($From)
$testRoot = $env:PUMP_TEST_ROOT
$what = 'install'; if ($Uninstall) { $what = 'uninstall' }

function Done([int]$code) {
    if (-not $testRoot) { Write-Host ''; Read-Host 'Press Enter to close' | Out-Null }
    exit $code
}

# ---- administrator rights (the system plug-in folders need them): ask once, run again elevated ---------------------
if (-not $testRoot) {
    $me = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$script`"", '-From', "`"$From`"")
        if ($Uninstall) { $a += '-Uninstall' }
        try { Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $a | Out-Null }
        catch { Write-Host "Windows didn't give permission, so nothing was changed. Run it again and click Yes."; Done 1 }
        exit 0
    }
}

# ---- where plugins live -------------------------------------------------------------------------------------------
if ($testRoot) {
    $common = Join-Path $testRoot 'Common Files'
    $userCommon = Join-Path $testRoot 'User\Programs\Common'
} else {
    # 64-bit Common Files even if a 32-bit PowerShell runs this
    $common = $env:CommonProgramW6432; if (-not $common) { $common = $env:CommonProgramFiles }
    $userCommon = Join-Path $env:LOCALAPPDATA 'Programs\Common'
}
$places = @(
    @{ Name = 'VST3';        Dir = (Join-Path $common 'VST3');     Pattern = 'pump-*.vst3'; Source = $From },
    @{ Name = 'CLAP';        Dir = (Join-Path $common 'CLAP');     Pattern = 'pump-*.clap'; Source = (Join-Path $From 'CLAP') },
    @{ Name = 'VST3 (user)'; Dir = (Join-Path $userCommon 'VST3'); Pattern = 'pump-*.vst3'; Source = $null },
    @{ Name = 'CLAP (user)'; Dir = (Join-Path $userCommon 'CLAP'); Pattern = 'pump-*.clap'; Source = $null }
)
$backup = Join-Path $common 'pump-previous'

function Plugin-Name([string]$item) { [System.IO.Path]::GetFileNameWithoutExtension($item) }

Write-Host ''
Write-Host "...pump $what"
Write-Host '==========================='

# ---- the new plugins (install only) -------------------------------------------------------------------------------
$new = @()
if (-not $Uninstall) {
    foreach ($p in $places) {
        if (-not $p.Source -or -not (Test-Path -LiteralPath $p.Source)) { continue }
        $new += @(Get-ChildItem -LiteralPath $p.Source -Filter $p.Pattern -ErrorAction SilentlyContinue | ForEach-Object { @{ Place = $p; Item = $_ } })
    }
    if (@($new | Where-Object { $_.Place.Name -eq 'VST3' }).Count -eq 0) {
        Write-Host "No pump plugins next to this installer (looked in $From)."
        Write-Host 'Unzip pump-windows.zip first, then run "Install pump.bat" from the unzipped folder.'
        Done 1
    }
}

# ---- what is installed now ----------------------------------------------------------------------------------------
$old = @()
foreach ($p in $places) {
    if (-not (Test-Path -LiteralPath $p.Dir)) { continue }
    $old += @(Get-ChildItem -LiteralPath $p.Dir -Filter $p.Pattern -ErrorAction SilentlyContinue | ForEach-Object { @{ Place = $p; Item = $_ } })
}

# ---- a host with a pump plugin open holds its files: change nothing, say which -------------------------------------
$locked = @()
foreach ($o in $old) {
    $files = @()
    if ($o.Item.PSIsContainer) { $files = @(Get-ChildItem -LiteralPath $o.Item.FullName -Recurse -File -ErrorAction SilentlyContinue) }
    else { $files = @($o.Item) }
    foreach ($f in $files) {
        try { $s = [System.IO.File]::Open($f.FullName, 'Open', 'ReadWrite', 'None'); $s.Close() }
        catch { $locked += (Plugin-Name $o.Item.Name); break }
    }
}
if ($locked.Count -gt 0) {
    Write-Host 'These plugins are open in a host (Ableton Live, Renoise...), so Windows will not let them be replaced:'
    $locked | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
    Write-Host 'Close the host and run this again. Nothing was changed.'
    Done 1
}

# ---- move the old ones into the backup (the last version only) ----------------------------------------------------
try {
    if ($old.Count -gt 0) {
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
        foreach ($o in $old) {
            $to = Join-Path $backup $o.Place.Name
            if (-not (Test-Path -LiteralPath $to)) { New-Item -ItemType Directory -Path $to -Force | Out-Null }
            Move-Item -LiteralPath $o.Item.FullName -Destination $to -Force
        }
    }

    # ---- copy the new ones in -------------------------------------------------------------------------------------
    foreach ($n in $new) {
        $dir = $n.Place.Dir
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -LiteralPath $n.Item.FullName -Destination $dir -Recurse -Force
        # downloaded files carry a "came from the internet" mark: the copies don't need it
        try { Get-ChildItem -LiteralPath (Join-Path $dir $n.Item.Name) -Recurse -File | Unblock-File } catch { }
    }
    if ($Uninstall -and (Test-Path -LiteralPath $backup)) { Remove-Item -LiteralPath $backup -Recurse -Force }
}
catch {
    Write-Host "Something went wrong: $($_.Exception.Message)"
    if (Test-Path -LiteralPath $backup) { Write-Host "The previous plugins are in $backup (copy them back into the VST3 / CLAP folders to undo)." }
    Done 1
}

# ---- what changed -------------------------------------------------------------------------------------------------
$oldNames = @($old | Where-Object { $_.Place.Name -eq 'VST3' -or $_.Place.Name -eq 'VST3 (user)' } | ForEach-Object { Plugin-Name $_.Item.Name } | Sort-Object -Unique)
$newNames = @($new | Where-Object { $_.Place.Name -eq 'VST3' } | ForEach-Object { Plugin-Name $_.Item.Name } | Sort-Object -Unique)
$userOld = @($old | Where-Object { $_.Place.Name -like '*(user)' }).Count
if ($Uninstall) {
    if ($old.Count -eq 0) { Write-Host 'No pump plugins were installed.' }
    else {
        Write-Host "Removed $($oldNames.Count) plugins:"
        Write-Host ('  ' + ($oldNames -join ', '))
    }
} else {
    $added = @($newNames | Where-Object { $oldNames -notcontains $_ })
    $gone = @($oldNames | Where-Object { $newNames -notcontains $_ })
    $kept = $newNames.Count - $added.Count
    Write-Host "Installed $($newNames.Count) plugins (VST3 in $(Join-Path $common 'VST3'), CLAP in $(Join-Path $common 'CLAP'))."
    if ($oldNames.Count -gt 0) { Write-Host "  updated: $kept" }
    if ($added.Count -gt 0) { Write-Host "  new:     $($added -join ', ')" }
    if ($gone.Count -gt 0) { Write-Host "  removed (no longer in pump): $($gone -join ', ')" }
    if ($userOld -gt 0) { Write-Host "  $userOld older copies in your user plug-in folders were taken out too." }
    if ($old.Count -gt 0) { Write-Host "The previous version is kept in $backup (copy it back to undo)." }
}
Write-Host ''
Write-Host 'In Ableton Live: Settings > Plug-Ins > Rescan (with "Use VST3 Plug-In System Folders" on).'
Done 0
