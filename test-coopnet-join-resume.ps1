param([ValidateRange(60,180)][int]$Seconds=110,[ValidateRange(60,180)][int]$ResumeSeconds=90,[switch]$AppearanceProbe,[switch]$MenuLeaveProbe,[switch]$VersionMismatchProbe)
$ErrorActionPreference='Stop'
$probeRoot=Join-Path $PSScriptRoot ('_build\coopnet-join-'+[Guid]::NewGuid().ToString('N'))
foreach ($role in @('host','guest')) {
    $cache=Join-Path $PSScriptRoot "_build\coopnet-engine-test\$role\appdata\shaders_cache"
    $target=Join-Path $probeRoot "$role\appdata"
    New-Item $target -ItemType Directory -Force | Out-Null
    if (Test-Path -LiteralPath $cache) { Copy-Item -LiteralPath $cache -Destination $target -Recurse }
}
$client=Join-Path (Split-Path $PSScriptRoot) 'Anomaly-1.5.3'
$hashes=@{}
Get-ChildItem "$client\appdata\savedgames\player - autosave.*" -File | ForEach-Object { $hashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName).Hash }
$options=Join-Path $client 'gamedata/configs/axr_options.ltx';$hashes[$options]=(Get-FileHash $options).Hash
& "$PSScriptRoot\prepare-coopnet-engine-test.ps1" -LoadFixture -ReplicaProbe -TestDirectory $probeRoot
foreach($role in @('host','guest')) {
    $root=Join-Path $probeRoot $role
    Copy-Item "$client/gamedata" $root -Recurse
    New-Item "$root/db" -ItemType Junction -Target "$client/db" | Out-Null
    $fs=Get-Content "$root/fsgame.ltx" | Where-Object {$_ -notmatch '^\$fs_root\$'}
    @('$fs_root$ = false | false | '+$root+'\')+@($fs) | Set-Content "$root/fsgame.ltx" -Encoding ascii
}
$guestConfig=Join-Path $probeRoot 'guest\appdata\user.ltx'
$guestSettings=@('coop_character_probe 127.0.0.1:27889')+@(Get-Content -LiteralPath $guestConfig | Where-Object {$_ -notmatch '^coop_'})
$guestSettings | Set-Content -LiteralPath $guestConfig -Encoding ascii
$owned=@()
function Start-OwnedProbe([string]$Role) {
    $root=Join-Path $probeRoot $Role
    $arguments=@('-silent_error_mode','-noprefetch')
    if($AppearanceProbe){$arguments+='-coop_appearance_probe'}
    if($MenuLeaveProbe -and $Role -eq 'guest'){$arguments+='-coop_menu_leave_probe'}
    if($VersionMismatchProbe -and $Role -eq 'guest'){$arguments+='-coop_version_mismatch_probe'}
    Start-Process -FilePath (Join-Path $root 'bin\AnomalyDX11.exe') -WorkingDirectory $root -ArgumentList $arguments -WindowStyle Hidden -PassThru
}
function Close-OwnedProbe($Process) {
    if (!$Process.HasExited) {
        $Process.CloseMainWindow() | Out-Null
        if (!$Process.WaitForExit(30000)) { Stop-Process -Id $Process.Id; throw 'Owned probe did not shut down normally.' }
    }
}
function Wait-OwnedPhase($Processes,[int]$Duration=$Seconds) {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Duration) {
        foreach ($process in $Processes) { if ($process.HasExited) { throw "Owned probe exited early: $($process.Id)" } }
        Start-Sleep -Milliseconds 500
    }
}
try {
    $hostProcess=Start-OwnedProbe 'host'; $owned+=$hostProcess
    $guestProcess=Start-OwnedProbe 'guest'; $owned+=$guestProcess
    Wait-OwnedPhase @($hostProcess,$guestProcess)
    Close-OwnedProbe $guestProcess
    if($VersionMismatchProbe) {
        Close-OwnedProbe $hostProcess
        $guestLog=Get-Content (Join-Path $probeRoot 'guest/appdata/logs/xray_deadparrot.log') -Raw
        $hostLog=Get-Content (Join-Path $probeRoot 'host/appdata/logs/xray_deadparrot.log') -Raw
        if($guestLog -notmatch 'version mismatch popup displayed: Host is using CoopNet 26.*mods 1.*Your version is CoopNet 26.*mods 3' -or
            $hostLog -notmatch 'radio version mismatch: ".+" attempted to join but a version mismatch was detected' -or
            ($guestLog+$hostLog) -match 'FATAL ERROR|\[SCRIPT ERROR\]|CoopNet update failed' -or
            $hostLog -match 'native guest bound:') {throw "Native version rejection notice failed: $probeRoot"}
        Write-Output "NATIVE_VERSION_MISMATCH_PASS: host radio named the rejected guest; guest returned to the menu and displayed host/local versions. Logs: $probeRoot"
        return
    }
    $credentials=Join-Path $probeRoot 'guest\appdata\coopnet-connections.dat'
    if (!(Test-Path -LiteralPath $credentials) -or (Get-Item -LiteralPath $credentials).Length -eq 0) { throw 'Saved encrypted credentials missing.' }
    $firstLog=Get-Content (Join-Path $probeRoot 'guest\appdata\logs\xray_deadparrot.log') -Raw
    if ($firstLog -notmatch 'CoopNet connection credentials saved: generation 1' -or $firstLog -notmatch 'guest arrival placed:' -or $firstLog -match 'FATAL ERROR|\[SCRIPT ERROR\]|CoopNet update failed|CoopNet options .* failed') { throw 'First admission or arrival failed.' }
    Set-Content (Join-Path $probeRoot 'guest-first.log') $firstLog
    Write-Output 'NATIVE_JOIN_CREDENTIALS_PASS: successful join wrote a Windows-encrypted connection profile.'
    if($MenuLeaveProbe) {
        Close-OwnedProbe $hostProcess
        $hostLog=Get-Content (Join-Path $probeRoot 'host/appdata/logs/xray_deadparrot.log') -Raw
        if($firstLog -notmatch 'menu leave probe: Join reopened after exit' -or
            $firstLog -notmatch 'menu leave probe: Rejoin last host started' -or
            $firstLog -notmatch 'Join menu entry inserted above New Game' -or
            $firstLog -notmatch 'connection credentials saved: generation 2' -or
            ([regex]::Matches($firstLog,'guest arrival placed:').Count -lt 2) -or
            ([regex]::Matches($hostLog,'native guest bound:').Count -lt 2) -or
            ($firstLog+$hostLog) -match 'FATAL ERROR|\[SCRIPT ERROR\]|CoopNet update failed|menu leave probe failed') {throw "Normal menu leave/rejoin failed: $probeRoot"}
        Write-Output "NATIVE_MENU_LEAVE_REJOIN_PASS: exited through normal disconnect, Join returned above New Game, and Rejoin last host resumed generation 2 and arrived in the host world. Screenshots: $probeRoot"
        return
    }
    # The same backend used by the menu now resolves the saved address, character and token.
    @($guestSettings) |
        Set-Content -LiteralPath $guestConfig -Encoding ascii
    $guestReopened=Start-OwnedProbe 'guest'; $owned+=$guestReopened
    Wait-OwnedPhase @($hostProcess,$guestReopened) $ResumeSeconds
    Close-OwnedProbe $guestReopened
    Close-OwnedProbe $hostProcess
    $guestLog=Get-Content (Join-Path $probeRoot 'guest\appdata\logs\xray_deadparrot.log') -Raw
    $hostLog=Get-Content (Join-Path $probeRoot 'host\appdata\logs\xray_deadparrot.log') -Raw
    if ($guestLog -notmatch 'CoopNet connection credentials saved: generation 2' -or
        $guestLog -notmatch 'CoopNet canonical baseline loaded and acknowledged:' -or
        $guestLog -notmatch 'CoopNet guest arrival placed:' -or
        $guestLog -notmatch 'CoopNet host world rules applied:' -or
        ([regex]::Matches($hostLog,'CoopNet native guest bound: object \d+ generation \d+').Count -lt 2) -or
        ($guestLog+$hostLog) -match 'FATAL ERROR|\[SCRIPT ERROR\]|CoopNet update failed:|CoopNet options .* failed') { throw 'New-process menu-backend resume, arrival or host settings evidence missing.' }
    if($AppearanceProbe) {
        $equipped=[regex]::Matches($hostLog,'appearance probe equipped: object (\d+) weapon (\S+) outfit (\S+) body (\S+)')
        $groups=$equipped | Group-Object {$_.Groups[1].Value}
        if(@($groups | Where-Object {$_.Count -ge 2}).Count -lt 2){throw "Both native players did not change equipped weapon and outfit: $probeRoot"}
        $appearances=[regex]::Matches($firstLog,'remote appearance applied: body (\S+) weapon (\S+) animation')
        if(@($appearances | ForEach-Object {$_.Groups[1].Value} | Sort-Object -Unique).Count -lt 2 -or
            @($appearances | ForEach-Object {$_.Groups[2].Value} | Sort-Object -Unique).Count -lt 2){throw "Remote host weapon/outfit changes missing: $probeRoot"}
        if($firstLog -notmatch 'remote held weapon rendered:' -or $guestLog -notmatch 'remote held weapon rendered:'){throw "Held weapon was not submitted to the renderer on join and reconnect: $probeRoot"}
        Write-Output "NATIVE_APPEARANCE_PASS: both native players changed weapons/outfits; guest received host model changes and submitted held weapon visuals before and after reconnect. Screenshots/logs: $probeRoot"
    }
    Write-Output "NATIVE_MENU_BACKEND_RESUME_PASS: reopened guest used saved credentials, resumed generation 2 against the running host, loaded its world and retained the settings lock. Test files: $probeRoot"
} finally {
    foreach ($process in $owned) { if (!$process.HasExited) { $process.CloseMainWindow() | Out-Null; if (!$process.WaitForExit(30000)) { Stop-Process -Id $process.Id } } }
    foreach ($path in $hashes.Keys) { if ((Get-FileHash -LiteralPath $path).Hash -ne $hashes[$path]) { throw "Original fixture source changed: $path" } }
}
