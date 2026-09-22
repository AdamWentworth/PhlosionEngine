[CmdletBinding()]
param(
    [string]$EngineRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [string]$RunId = (Get-Date -Format 'yyyyMMdd-HHmmss'),
    [string[]]$Apis = @('opengl','vulkan','d3d12'),
    [string[]]$Cases = @('walk','run','wall-blocked','ramp','stairs','trigger-active','camera-obstructed','recovered-reset'),
    [string[]]$Aspects = @('16:9','21:9'),
    [switch]$Probe,
    [switch]$Supplemental
)
$ErrorActionPreference='Stop'
if($Supplemental){$Cases=@('steep','ledge-drop');$Aspects=@('16:9')}
Import-Module (Join-Path $PSScriptRoot '../public_asset_fixture/FixtureProcess.psm1') -Force
if($RunId -notmatch '^[A-Za-z0-9._-]+$'){throw 'Invalid run ID'}
$EngineRoot=(Resolve-Path -LiteralPath $EngineRoot).Path
$run=Join-Path $EngineRoot "artifacts/m2-f/$RunId"
if(Test-Path -LiteralPath $run){throw 'Refusing to reuse an existing run directory'}
New-Item -ItemType Directory $run | Out-Null
$bin=Join-Path $EngineRoot 'build-presentation/Debug'
$runtime=Join-Path $bin 'ExplorationFixtureRuntime.exe'
$editor=Join-Path $bin 'PhlosionEditor.exe'
$plugin=Join-Path $bin 'ExplorationFixtureEditorPlugin.dll'
$guard=Join-Path $bin 'ExplorationFixtureGuard.exe'
function Identity([string]$path){$file=Get-Item -LiteralPath $path;return [ordered]@{path=$file.FullName;bytes=[uint64]$file.Length;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}}
$binaries=@(([ordered]@{role='runtime'}+(Identity $runtime)),([ordered]@{role='editor'}+(Identity $editor)),([ordered]@{role='plugin'}+(Identity $plugin)),([ordered]@{role='guard'}+(Identity $guard)))
$sourcePaths=@('CMakeLists.txt','src/engine/editor/PhlosionEditorMain.cpp','src/engine/editor/EditorShell.cpp','src/engine/editor/EditorShell.h','src/engine/editor/EditorSdlGameInput.h','src/engine/editor/EditorProjectPlugin.h','src/engine/input/InputActions.cpp','src/engine/input/InputActions.h','src/engine/input/SdlActionInput.cpp','src/engine/input/SdlActionInput.h','src/engine/input/SdlGameInput.cpp','src/engine/input/SdlGameInput.h','src/engine/input/SdlInputReplay.cpp','src/engine/input/SdlInputReplay.h','src/engine/collision/CollisionWorld.cpp','src/engine/camera/ExplorationCamera.cpp','tests/TestExplorationFixture.cpp')
$sourcePaths+=@(Get-ChildItem (Join-Path $EngineRoot 'examples/exploration_fixture') -File | ForEach-Object {$_.FullName})
$sourcePaths+=@(Get-ChildItem (Join-Path $EngineRoot 'tools/exploration_fixture') -File | ForEach-Object {$_.FullName})
$sourcePaths+=@('tests/exploration_fixture/test_native_harness.ps1','tests/TestEditorSdlExtensionPlugin.cpp','src/engine/editor/EditorProjectPluginProbe.cpp')
$sourcePaths+=@('src/engine/collision/CollisionWorld.h','src/engine/camera/ExplorationCamera.h','src/engine/platform/Window.cpp','src/engine/platform/Window.h','tools/public_asset_fixture/FixtureProcess.psm1')
$sources=@($sourcePaths | ForEach-Object {if([IO.Path]::IsPathRooted($_)){Identity $_}else{Identity (Join-Path $EngineRoot $_)}})
$scripts=Join-Path $run 'scripts'
$scriptResult=Invoke-FixtureProcess -FilePath $guard -Arguments @('scripts',$scripts) -WorkingDirectory $EngineRoot
$scriptResult | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $run 'scripts-process.json')
if($scriptResult.ExitCode -ne 0){throw $scriptResult.Stderr}
$project=Join-Path $run 'project'
$pluginDirectory=Join-Path $project '.phlosion/editor/Debug'
New-Item -ItemType Directory -Force $pluginDirectory | Out-Null
Copy-Item -LiteralPath $plugin -Destination $pluginDirectory
$descriptor=Join-Path $project 'phlosion.project.json'
$description=[ordered]@{
    schema_version=2;project_id='phlosion-exploration-fixture';display_name='Public Exploration Course'
    content_mounts=@(@{id='fixture';root='content';required=$true})
    startup_scene=@{scene_id='fixture/scene'}
    environments=@(@{asset_id='fixture/environment';display_name='Public Course';kind='cooked';mount='fixture';path='scenes/fixture.phscene'})
    scenes=@(@{scene_id='fixture/scene';display_name='Exploration';category='Samples';environment_asset_id='fixture/environment';status='implemented'})
    editor_plugin=@{library='ExplorationFixtureEditorPlugin';directory='.phlosion/editor/{config}'}
}
[IO.File]::WriteAllText($descriptor,($description|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
$captures=New-Object System.Collections.Generic.List[object]
$failures=New-Object System.Collections.Generic.List[string]
foreach($aspect in $Aspects){foreach($api in $Apis){foreach($case in $Cases){
    $width=640;$height=360
    foreach($hostName in @('editor','runtime')){
        $label="$($aspect.Replace(':','-'))/$api/$case/$hostName"
        $dir=Join-Path $run $label;New-Item -ItemType Directory -Force $dir | Out-Null
        $png=Join-Path $dir 'capture.png';$handshake=Join-Path $dir 'handshake.json';$trace=Join-Path $dir 'trace.jsonl';$metrics=Join-Path $dir 'metrics.json'
        $script=Join-Path $scripts "$case.json";$state=Join-Path $dir 'editor-state'
        $overrides=@{}
        Get-ChildItem Env: | Where-Object {$_.Name -like 'PHLOSION_*'} | ForEach-Object {$overrides[$_.Name]=''}
        $overrides['PHLOSION_BACKEND_SCREENSHOT_PATH']=$png;$overrides['PHLOSION_BACKEND_SCREENSHOT_FRAME']='160';$overrides['PHLOSION_BACKEND_SCREENSHOT_DEFER']='0'
        $overrides['PHLOSION_PARITY_CONTRACT_FATAL']='1';$overrides['PHLOSION_EXPLORATION_AUTOMATION']='1';$overrides['PHLOSION_EXPLORATION_API']=$api
        # Child environment only: .NET ProcessStartInfo never mutates runner/user hints.
        $overrides['SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS']='1'
        $overrides['PHLOSION_EXPLORATION_CASE']=$case;$overrides['PHLOSION_EXPLORATION_HANDSHAKE']=$handshake;$overrides['PHLOSION_EXPLORATION_TRACE']=$trace
        if($hostName -eq 'editor'){
            $exe=$editor;$arguments=@("--project=$descriptor","--renderer=$api",'--hidden',"--state-directory=$state",'--fixed-delta=0.016666667','--frames=164',"--metrics-output=$metrics",'--game-preview=exploration','--play-game-preview',"--input-replay=$script","--game-aspect=$aspect")
        }else{
            $exe=$runtime;$arguments=@("--api=$api","--case=$case",'--hidden','--frames=164',"--width=$width","--height=$height","--input-replay=$script","--handshake=$handshake","--trace=$trace")
        }
        $started=(Get-Date).ToUniversalTime()
        $result=Invoke-FixtureProcess -FilePath $exe -Arguments $arguments -WorkingDirectory $EngineRoot -EnvironmentOverrides $overrides -TimeoutSeconds 120
        $process=Join-Path $dir 'process.json';$stdout=Join-Path $dir 'stdout.log';$stderr=Join-Path $dir 'stderr.log'
        $result|ConvertTo-Json -Depth 6|Set-Content $process
        [IO.File]::WriteAllText($stdout,$result.Stdout);[IO.File]::WriteAllText($stderr,$result.Stderr)
        [IO.File]::WriteAllText((Join-Path $dir 'environment.json'),($overrides|ConvertTo-Json -Depth 6))
        if($result.ExitCode -ne 0 -or $result.TimedOut){$failures.Add("$label exit=$($result.ExitCode) timeout=$($result.TimedOut)")}
        try{
            $h=Get-Content $handshake -Raw|ConvertFrom-Json
            if($hostName -eq 'editor'){$width=[int]$h.width;$height=[int]$h.height}
            $size=Get-FixturePngSize $png
            $records=@(Get-FixtureScreenshotRecords ($result.Stdout+"`n"+$result.Stderr))
            if($records.Count -ne 1 -or $records[0].Status -ne 'WROTE' -or $records[0].Frame -ne 160){throw 'Wrong screenshot record'}
            $record=$records[0]
            if([IO.Path]::GetFullPath($record.Path) -ne [IO.Path]::GetFullPath($png) -or $record.Width -ne $size.Width -or $record.Height -ne $size.Height){throw 'Screenshot path/dimensions disagree'}
            $display=@{opengl='OpenGL';vulkan='Vulkan';d3d12='D3D12'}[$api]
            if($record.Backend -ne $display -or ($result.Stdout+$result.Stderr) -notmatch [regex]::Escape("[ParityContract][$display] PASS")){throw 'Native backend contract not established'}
            if((Get-Item -LiteralPath $png).LastWriteTimeUtc -lt $started){throw 'Stale screenshot'}
            $row=[ordered]@{case=$case;host=$hostName;api=$api;aspect=$aspect;requested_frame=160;observed_frame=$record.Frame;window_width=$size.Width;window_height=$size.Height;started_utc=$started.ToString('o');png=(Identity $png);handshake=(Identity $handshake);trace=(Identity $trace);script=(Identity $script);stdout=(Identity $stdout);stderr=(Identity $stderr);process=(Identity $process);environment=(Identity (Join-Path $dir 'environment.json'))}
            if($hostName -eq 'editor'){$row.metrics=Identity $metrics}
            $captures.Add($row)
        }catch{$failures.Add("${label}: $($_.Exception.Message)")}
        Write-Host "[Exploration] $label exit=$($result.ExitCode) viewport=${width}x${height}"
    }
}}}
$schema=if($Supplemental){'phlosion-exploration-supplemental-v1'}else{'phlosion-exploration-matrix-v1'}
$manifest=[ordered]@{schema=$schema;baseline=(& git -C $EngineRoot rev-parse HEAD|Out-String).Trim();source_root=$EngineRoot;source_status=@(& git -C $EngineRoot status --porcelain);binaries=$binaries;project=@{descriptor=(Identity $descriptor);plugin=(Identity (Join-Path $pluginDirectory 'ExplorationFixtureEditorPlugin.dll'))};sources=$sources;thresholds=@{normalized_mae=.01;normalized_rmse=.08;changed_ratio=.05;channel_tolerance=8};captures=$captures;failures=$failures}
$manifestPath=Join-Path $run 'manifest.json'
[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
if($failures.Count){$failures|ForEach-Object {Write-Host $_};throw "Capture failures: $($failures.Count)"}
if(-not $Probe){
    $check=Invoke-FixtureProcess -FilePath $guard -Arguments @('matrix',$manifestPath) -WorkingDirectory $EngineRoot -TimeoutSeconds 300
    $check|ConvertTo-Json -Depth 6|Set-Content (Join-Path $run 'guard-process.json')
    [IO.File]::WriteAllText((Join-Path $run 'guard.log'),($check.Stdout+"`n"+$check.Stderr))
    Write-Host $check.Stdout
    if($check.ExitCode -ne 0){throw $check.Stderr}
}
Write-Host "Evidence: $manifestPath"
