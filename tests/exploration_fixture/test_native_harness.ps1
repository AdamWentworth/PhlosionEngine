[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Manifest,[Parameter(Mandatory=$true)][string]$GuardExe)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot '../../tools/public_asset_fixture/FixtureProcess.psm1') -Force
$Manifest=(Resolve-Path -LiteralPath $Manifest).Path;$GuardExe=(Resolve-Path -LiteralPath $GuardExe).Path
$root=Split-Path -Parent $Manifest;$original=[IO.File]::ReadAllText($Manifest)
$output=Join-Path $root 'negative-evidence';New-Item -ItemType Directory -Force $output | Out-Null
$working=Join-Path $root 'negative-manifest.json'
function Identity([string]$path){$f=Get-Item -LiteralPath $path;return [pscustomobject]@{path=$f.FullName;bytes=[uint64]$f.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}}
function SaveJson([string]$path,$value){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
function Backup([string]$path){if(-not $script:backups.ContainsKey($path)){$script:backups[$path]=[IO.File]::ReadAllBytes($path)}}
function ChangeJson($row,[string]$role,[scriptblock]$change){$path=$row.$role.path;Backup $path;$j=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;& $change $j;SaveJson $path $j;$row.$role=Identity $path}
function CheckRun([string]$name,[bool]$pass,[string]$manifestPath){
    $r=Invoke-FixtureProcess -FilePath $GuardExe -Arguments @('matrix',$manifestPath) -WorkingDirectory $root -TimeoutSeconds 300
    $r|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output "$name-process.json")
    [IO.File]::WriteAllText((Join-Path $output "$name.log"),($r.Stdout+"`n"+$r.Stderr))
    if($r.TimedOut -or (($r.ExitCode -eq 0) -ne $pass)){throw "Unexpected guard result for $name exit=$($r.ExitCode) timeout=$($r.TimedOut)"}
    Write-Host "[Exploration harness] $name expected=$pass exit=$($r.ExitCode)"
}
CheckRun 'pristine-before' $true $Manifest
$cases=[ordered]@{
    'missing-case'={param($m,$r)$m.captures=@($m.captures|Select-Object -Skip 1)}
    'extra-case'={param($m,$r)$m.captures+=@($r)}
    'duplicate-case'={param($m,$r)$m.captures[1]=$r}
    'png-hash'={param($m,$r)$r.png.sha256='0'*64}
    'binary-hash'={param($m,$r)$m.binaries[0].sha256='0'*64}
    'source-hash'={param($m,$r)$m.sources[0].sha256='0'*64}
    'missing-required-source'={param($m,$r)$m.sources=@($m.sources|Where-Object {$_.path -notlike '*\SdlInputReplay.cpp'})}
    'wrong-descriptor'={param($m,$r)ChangeJson $m.project 'descriptor' {param($j)$j.editor_plugin.library='UnrelatedPlugin'}}
    'wrong-staged-plugin'={param($m,$r)
        $path=$m.project.plugin.path;Backup $path;$bytes=[IO.File]::ReadAllBytes($path);$bytes[0]=$bytes[0] -bxor 1
        [IO.File]::WriteAllBytes($path,$bytes);$m.project.plugin=Identity $path
    }
    'wrong-project-argument'={param($m,$r)ChangeJson $r 'process' {param($j)$j.CommandLine=$j.CommandLine.Replace('--project=','--project=wrong-')}}
    'outside-path'={param($m,$r)$r.png=$m.binaries[0]}
    'capture-frame'={param($m,$r)$r.observed_frame=159}
    'wrong-backend'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.api='vulkan'}}
    'wrong-tick'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.state.tick=119}}
    'wrong-aspect'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.width++}}
    'wrong-player'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.state.player[0]+=1}}
    'wrong-camera'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.state.camera.yaw+=1}}
    'wrong-sensor'={param($m,$r)ChangeJson $r 'handshake' {param($j)$j.state.sensor=$true}}
    'wrong-background-mode'={param($m,$r)ChangeJson $r 'environment' {param($j)$j.SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS='0'}}
    'wrong-script'={param($m,$r)ChangeJson $r 'script' {param($j)$j.events[0].frame=2}}
    'wrong-process'={param($m,$r)ChangeJson $r 'process' {param($j)$j.CommandLine='unrelated.exe'}}
    'missing-trace-tick'={param($m,$r)
        $path=$r.trace.path;Backup $path;$lines=[IO.File]::ReadAllLines($path);$removed=$false
        $filtered=@($lines|Where-Object {if(-not $removed -and $_ -match '"kind":"tick"'){$removed=$true;$false}else{$true}})
        [IO.File]::WriteAllLines($path,$filtered,[Text.UTF8Encoding]::new($false));$r.trace=Identity $path
    }
    'empty-pixels'={param($m,$r)$script:pixelDefect='blank'}
    'absent-player-pixels'={param($m,$r)$script:pixelDefect='erase-player'}
    'missing-wall-pixels'={param($m,$r)$script:pixelDefect='erase-wall'}
    'wrong-view-pixels'={param($m,$r)$script:pixelDefect='mirror'}
    'wrong-sensor-pixels'={param($m,$r)$script:pixelDefect='erase-sensor';$script:pixelRow=@($m.captures|Where-Object {$_.case -eq 'trigger-active'})[0]}
}
foreach($name in $cases.Keys){
    $script:backups=@{};$script:pixelDefect='';$m=$original|ConvertFrom-Json;$row=$m.captures[0];$script:pixelRow=$row
    try{
        & $cases[$name] $m $row
        if($script:pixelDefect){
            $path=$script:pixelRow.png.path;Backup $path
            $damage=Invoke-FixtureProcess -FilePath $GuardExe -Arguments @($script:pixelDefect,$path) -WorkingDirectory $root
            $damage|ConvertTo-Json -Depth 6|Set-Content (Join-Path $output "$name-mutation.json")
            if($damage.ExitCode -ne 0){throw $damage.Stderr}
            $script:pixelRow.png=Identity $path
        }
        SaveJson $working $m;CheckRun $name $false $working
    }finally{foreach($path in $script:backups.Keys){[IO.File]::WriteAllBytes($path,$script:backups[$path])}}
}
CheckRun 'pristine-after' $true $Manifest
Write-Host "Native evidence harness PASS: $($cases.Count) mutations and pristine recovery"
