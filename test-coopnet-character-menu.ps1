$ErrorActionPreference='Stop'
$testRoot=Join-Path $PSScriptRoot ('_build/coopnet-character-menu-'+[Guid]::NewGuid().ToString('N'))
& "$PSScriptRoot/prepare-coopnet-engine-test.ps1" -TestDirectory $testRoot
foreach ($selection in @('join','load','create')) {
    $role=if ($selection -eq 'create') {'guest'} else {'host'}
    $root=Join-Path $testRoot $role
    $config=Join-Path $root 'appdata/user.ltx'
    Get-Content $config | Where-Object {$_ -notmatch '^(coop_|start )'} | Set-Content "$config.tmp" -Encoding ascii
    Move-Item -LiteralPath "$config.tmp" -Destination $config -Force
    $flag=if($selection -eq 'join'){'-coop_menu_probe'}else{"-coop_character_${selection}_menu_probe"}
    $owned=Start-Process -FilePath "$root/bin/AnomalyDX11.exe" -WorkingDirectory $root -ArgumentList '-silent_error_mode','-noprefetch',$flag -WindowStyle Hidden -PassThru
    try {
        Start-Sleep -Seconds 12
        if ($owned.HasExited) {throw "Native $selection selector exited early"}
    } finally {
        if (!$owned.HasExited) {
            $owned.CloseMainWindow() | Out-Null
            if (!$owned.WaitForExit(10000)) {Stop-Process -Id $owned.Id; throw "Owned $selection menu process did not close normally"}
        }
    }
    $log=(Get-ChildItem "$root/appdata/logs" -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
    $output=Get-Content $log -Raw
    if($selection -eq 'join') {
        if($output -notmatch 'Join menu entry inserted above New Game' -or $output -notmatch 'Join address dialog opened' -or $output -notmatch 'join dialog screenshot captured' -or $output -match 'FATAL ERROR|\[SCRIPT ERROR\]') {throw 'Native Join menu/dialog failed'}
        Write-Host "PASS: native Join entry and stacked dialog; screenshot: $root/appdata/screenshots"
        continue
    }
    if ($output -notmatch "native character menu probe passed: $selection" -or $output -match 'FATAL ERROR|native character menu probe failed') {throw "Stock $selection menu callback did not open its dialog"}
    Write-Host "PASS: native $selection selector callback opened its dialog. Log: $log"
}
