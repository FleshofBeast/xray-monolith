$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
Set-Location $repo
$root=Join-Path (Join-Path $repo '_build') ('native-world-'+[Guid]::NewGuid().ToString('N'))
$source=Join-Path (Split-Path $repo) 'Anomaly-1.5.3/appdata/savedgames'
$hashes=@{};Get-ChildItem $source -Filter 'player - autosave.*' -File | ForEach-Object {$hashes[$_.FullName]=(Get-FileHash $_.FullName).Hash}
$owned=@()
try {
 $owned=@(& "$repo/prepare-coopnet-engine-test.ps1" -Launch -LoadFixture -ReplicaProbe -MovementProbe -WorldProbe -WeaponProbe -SharedWorldProbe -NativeWorldProbe -TestDirectory $root)
 if($owned.Count -ne 2){throw 'Expected exactly two fixture processes'}
 $watch=[Diagnostics.Stopwatch]::StartNew()
 while($watch.Elapsed.TotalSeconds -lt 120){foreach($process in $owned){if($process.HasExited){throw "Fixture exited early: $($process.Id) code $($process.ExitCode)"}};Start-Sleep -Milliseconds 500}
} finally {
 $cleanupErrors=@()
 foreach($process in $owned){if(!$process.HasExited){$process.CloseMainWindow()|Out-Null;if(!$process.WaitForExit(30000)){Stop-Process -Id $process.Id;$cleanupErrors+='Owned fixture did not close normally'}}}
 if($cleanupErrors.Count){Write-Warning ($cleanupErrors -join '; ')}
 foreach($path in $hashes.Keys){if((Get-FileHash $path).Hash -ne $hashes[$path]){throw 'Private source save was changed'}}
}
$logs=@{};foreach($role in @('host','guest')){$file=Get-ChildItem "$root/$role/appdata/logs" -Filter '*.log'|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1;$logs[$role]=Get-Content $file.FullName -Raw;if($logs[$role] -match 'FATAL ERROR|! CoopNet update failed:|UnhandledFilter'){throw "$role runtime failed"};if($logs[$role] -notmatch 'CoopNet session stopped'){throw "$role shutdown missing"}}
if($logs.guest -notmatch 'CoopNet client state: connected' -or $logs.guest -notmatch 'CoopNet native stalker frame completed: object'){throw 'Connected guest native NPC frame completion missing'}
foreach($marker in @('host NPC and quests created','host NPC killed and quests completed/failed','host corpse removed and info withdrawn')){if(!$logs.host.Contains($marker)){throw "Host shared-world fixture incomplete: $marker"}}
foreach($marker in @('CoopNet NPC spawned: section dog_weak','CoopNet NPC death applied:','CoopNet NPC removed:')){if(!$logs.guest.Contains($marker)){throw "Guest shared-world lifecycle missing: $marker"}}
$dogMatch=[regex]::Match($logs.guest,'CoopNet NPC spawned: section dog_weak anchor (\d+)')
if(!$dogMatch.Success){throw 'Shared enemy identity missing'}
$dogAnchor=$dogMatch.Groups[1].Value
if(!$logs.guest.Contains("CoopNet NPC death applied: anchor $dogAnchor") -or !$logs.guest.Contains("CoopNet NPC removed: anchor $dogAnchor section dog_weak")){throw 'Shared enemy death/removal did not match its spawned identity'}
if($logs.guest -notmatch 'CoopNet NPC spawned:[^\r\n]*trader 1 visible 1'){throw 'Native trader recreation missing'}
if($logs.guest.Contains('CoopNet quests applied:')){throw 'Personal-quest mode applied a host quest snapshot'}
if($cleanupErrors.Count){throw ($cleanupErrors -join '; ')}
Write-Output "NATIVE_WORLD_PROTOTYPE_PASS: $root; native NPC frames completed, both clients closed, source saves unchanged. Personal quests/loot persistence and combat synchronization remain unverified."


