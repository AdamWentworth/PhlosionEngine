# Negative and adversarial tests for the material fixture harness.
#
# Runs without a GPU: synthetic captures and handshakes come from the guard's
# hermetic `synth` command, and process handling is exercised through the guard
# `echo` helper plus a harmless two-level PowerShell child.

[CmdletBinding()]
param(
    [string]$EngineRoot = '',
    [string]$ProcessModule = '',
    [Parameter(Mandatory = $true)][string]$GuardExe
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    $EngineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}
if ([string]::IsNullOrWhiteSpace($ProcessModule)) {
    $ProcessModule = Join-Path $PSScriptRoot '..\..\tools\public_asset_fixture\FixtureProcess.psm1'
}
Import-Module (Resolve-Path -LiteralPath $ProcessModule).Path -Force

$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$guardPath = (Resolve-Path -LiteralPath $GuardExe).Path
$workRoot = $null
for ($attempt = 0; $attempt -lt 8 -and $null -eq $workRoot; ++$attempt) {
    $candidate = Join-Path ([System.IO.Path]::GetTempPath()) (
        'phlosion material fixture harness [x] & y ' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $candidate -ErrorAction Stop | Out-Null
        $workRoot = $candidate
    } catch {
        Write-Host "[MaterialFixture][Harness] temp directory collision, retrying: $candidate"
    }
}
if ($null -eq $workRoot) {
    throw 'could not acquire a unique material fixture harness directory'
}

$failures = New-Object System.Collections.Generic.List[string]
function Check-Fixture([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        $failures.Add($Message)
    }
}

function Write-FixtureJson([string]$Path, $Document) {
    [System.IO.File]::WriteAllText(
        $Path,
        ($Document | ConvertTo-Json -Depth 12),
        [System.Text.UTF8Encoding]::new($false))
}

$hosts = @('runtime', 'editor')
$apis = @('opengl', 'vulkan', 'd3d12')
$cases = @(
    @{ name = 'authored-maps'; frame = 0 },
    @{ name = 'empty-image-bytes'; frame = 60 },
    @{ name = 'absent-maps'; frame = 120 }
)
$captureFrames = @{ runtime = 4; editor = 6 }
$approvedThresholds = [ordered]@{
    mae = 0.01
    rmse = 0.08
    changed_ratio = 0.05
    channel_tolerance = 8
}
$revision = 'a037f8833cb3a509b980b825fc5833565eac4784'

$guardItem = Get-Item -LiteralPath $guardPath
$binaryIdentity = [ordered]@{
    path = $guardItem.FullName
    sha256 = (Get-FileHash -LiteralPath $guardPath -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes = [uint64]$guardItem.Length
}

$entries = New-Object System.Collections.Generic.List[object]
foreach ($hostName in $hosts) {
    foreach ($api in $apis) {
        foreach ($case in $cases) {
            $casePng = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.png')))
            $caseHandshake = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.json')))
            $caseStdout = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.out.log')))
            $caseStderr = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.err.log')))
            $caseMetrics = Join-Path $workRoot (
                Join-Path $hostName (Join-Path $api ($case.name + '.metrics.json')))
            New-Item -ItemType Directory -Path (Split-Path -Parent $casePng) -Force |
                Out-Null
            $screenshotFrame = $captureFrames[$hostName]
            $display = switch ($api) {
                'opengl' { 'OpenGL' }
                'vulkan' { 'Vulkan' }
                'd3d12' { 'D3D12' }
            }
            $synth = Invoke-FixtureProcess `
                -FilePath $guardPath `
                -Arguments @(
                    'synth',
                    "--out-png=$casePng",
                    "--out-handshake=$caseHandshake",
                    "--host=$hostName",
                    "--api=$api",
                    "--case=$($case.name)",
                    '--defect=none',
                    '--surface=320x180') `
                -TimeoutSeconds 60
            Check-Fixture ($synth.ExitCode -eq 0) "synth $hostName/$api/$($case.name) failed: $($synth.Stderr)"
            [System.IO.File]::WriteAllText(
                $caseStdout,
                "[Screenshot][$display] WROTE $casePng size=320x180 frame=$screenshotFrame`n" +
                "[ParityContract][$display] PASS signature=0000000000000000`n")
            [System.IO.File]::WriteAllText($caseStderr, '')
            if ($hostName -eq 'editor') {
                Write-FixtureJson $caseMetrics ([ordered]@{
                    schema = 'phlosion-editor-baseline-metrics-v1'
                    capture = [ordered]@{
                        frames = 12
                        width = 320
                        height = 180
                        viewport_width = 320
                        viewport_height = 180
                    }
                    renderer = [ordered]@{ backend = $api }
                })
            }
            $entries.Add([ordered]@{
                host = $hostName
                api = $api
                case = $case.name
                png = $casePng
                handshake = $caseHandshake
                stdout = $caseStdout
                stderr = $caseStderr
                metrics = $caseMetrics
                exit_code = 0
                requested_capture_frame = $screenshotFrame
                screenshot_backend = $display
                observed_capture_frame = $screenshotFrame
                screenshot_size = @(320, 180)
                logical_frame = $case.frame
            })
        }
    }
}

$baseManifest = [ordered]@{
    run_id = 'harness'
    engine_root = $engineRootPath
    build_directory = 'harness'
    configuration = 'Debug'
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    powershell_version = $PSVersionTable.PSVersion.ToString()
    dotnet_version = [System.Environment]::Version.ToString()
    source_revision = $revision
    source_baseline = $revision
    source_dirty = $true
    source_status = @(' M harness')
    editor_frames = 12
    capture_frames = [ordered]@{ runtime = $captureFrames.runtime; editor = $captureFrames.editor }
    process_timeout_seconds = 30
    binaries = [ordered]@{
        runtime = $binaryIdentity
        editor = $binaryIdentity
        plugin = $binaryIdentity
        guard = $binaryIdentity
    }
    thresholds = $approvedThresholds
    entries = $entries
}
$baseManifestPath = Join-Path $workRoot 'manifest-base.json'
Write-FixtureJson $baseManifestPath $baseManifest

function Invoke-GuardMatrix(
    [string]$ManifestPath,
    [string]$Name,
    [bool]$ExpectSuccess,
    [string]$OutPath = '',
    [string]$SheetPath = '') {
    $outPath = if ([string]::IsNullOrWhiteSpace($OutPath)) {
        Join-Path $workRoot "$Name-matrix.json"
    } else {
        $OutPath
    }
    $sheetPath = if ([string]::IsNullOrWhiteSpace($SheetPath)) {
        Join-Path $workRoot "$Name-sheet.png"
    } else {
        $SheetPath
    }
    $logPath = Join-Path $workRoot "$Name-guard.log"
    $result = Invoke-FixtureProcess `
        -FilePath $guardPath `
        -Arguments @(
            'matrix',
            "--manifest=$ManifestPath",
            "--out=$outPath",
            "--sheet=$sheetPath",
            '--require-complete') `
        -TimeoutSeconds 300
    [System.IO.File]::WriteAllText($logPath, ($result.Stdout + "`n" + $result.Stderr))
    if ($ExpectSuccess) {
        Check-Fixture ($result.ExitCode -eq 0) "guard matrix '$Name' should have passed: $($result.Stderr)$($result.Stdout)"
    } else {
        Check-Fixture ($result.ExitCode -ne 0) "guard matrix '$Name' should have failed"
    }
    $result | Add-Member -NotePropertyName MatrixPath -NotePropertyValue $outPath
    return $result
}

function Get-GuardFailureText($Result) {
    $text = $Result.Stdout + "`n" + $Result.Stderr
    if ($Result.MatrixPath -and (Test-Path -LiteralPath $Result.MatrixPath)) {
        try {
            $matrix = Get-Content -LiteralPath $Result.MatrixPath -Raw | ConvertFrom-Json
            foreach ($entry in $matrix.entries) {
                foreach ($failure in $entry.failures) { $text += "`n$failure" }
            }
            foreach ($pair in $matrix.pairwise) {
                foreach ($failure in $pair.failures) { $text += "`n$failure" }
            }
            foreach ($cross in $matrix.cross_case) {
                foreach ($failure in $cross.failures) { $text += "`n$failure" }
            }
            foreach ($failure in $matrix.manifest_failures) { $text += "`n$failure" }
        } catch {
        }
    }
    return $text
}

function Copy-And-MutateManifest([string]$Name, [scriptblock]$Mutation) {
    $document = Get-Content -LiteralPath $baseManifestPath -Raw | ConvertFrom-Json
    & $Mutation $document
    $path = Join-Path $workRoot "$Name-manifest.json"
    Write-FixtureJson $path $document
    return $path
}

# Positive coverage: the hermetic 18-entry Cartesian matrix must pass.
Invoke-GuardMatrix -ManifestPath $baseManifestPath -Name 'positive' -ExpectSuccess $true | Out-Null

$duplicatePath = Copy-And-MutateManifest 'duplicate' {
    param($document)
    $document.entries = @($document.entries[0]) * 18
}
$duplicateResult = Invoke-GuardMatrix -ManifestPath $duplicatePath -Name 'duplicate' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $duplicateResult) -match 'duplicate') `
    'duplicate manifest did not report a duplicate tuple'

$missingPath = Copy-And-MutateManifest 'missing' {
    param($document)
    $document.entries = @($document.entries[0..16])
}
$missingResult = Invoke-GuardMatrix -ManifestPath $missingPath -Name 'missing' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingResult) -match '18|missing') `
    'incomplete manifest did not report the missing tuple'

$unknownHostPath = Copy-And-MutateManifest 'unknown-host' {
    param($document)
    $document.entries[0].host = 'bogus'
}
$unknownHostResult = Invoke-GuardMatrix -ManifestPath $unknownHostPath -Name 'unknown-host' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $unknownHostResult) -match 'host is unknown') `
    'unknown host was not reported'

$unknownApiPath = Copy-And-MutateManifest 'unknown-api' {
    param($document)
    $document.entries[0].api = 'metal'
}
$unknownApiResult = Invoke-GuardMatrix -ManifestPath $unknownApiPath -Name 'unknown-api' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $unknownApiResult) -match 'API is unknown') `
    'unknown API was not reported'

$looseThresholdPath = Copy-And-MutateManifest 'loose-threshold' {
    param($document)
    $document.thresholds.mae = 0.5
}
$looseThresholdResult = Invoke-GuardMatrix -ManifestPath $looseThresholdPath -Name 'loose-threshold' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $looseThresholdResult) -match 'looser') `
    'loosened threshold was not reported'

$negativeThresholdPath = Copy-And-MutateManifest 'negative-threshold' {
    param($document)
    $document.thresholds.channel_tolerance = -1
}
$negativeThresholdResult = Invoke-GuardMatrix -ManifestPath $negativeThresholdPath -Name 'negative-threshold' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $negativeThresholdResult) -match 'tolerance') `
    'negative channel tolerance was not reported'

$baselinePath = Copy-And-MutateManifest 'baseline' {
    param($document)
    $document.source_baseline = '0000000000000000000000000000000000000000'
}
$baselineResult = Invoke-GuardMatrix -ManifestPath $baselinePath -Name 'baseline' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $baselineResult) -match 'baseline') `
    'source baseline mismatch was not reported'

# The older archive guard only compared the recorded hash shape and size; this
# guard re-hashes the final binaries and rejects a stale record.
$hashPath = Copy-And-MutateManifest 'binary-hash' {
    param($document)
    $document.binaries.guard.sha256 = '0' * 64
}
$hashResult = Invoke-GuardMatrix -ManifestPath $hashPath -Name 'binary-hash' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $hashResult) -match 'stale') `
    'a stale binary hash was not reported'

$sizePath = Copy-And-MutateManifest 'binary-size' {
    param($document)
    $document.binaries.guard.bytes = [uint64]1
}
$sizeResult = Invoke-GuardMatrix -ManifestPath $sizePath -Name 'binary-size' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $sizeResult) -match 'size') `
    'a binary size mismatch was not reported'

$missingBinariesPath = Copy-And-MutateManifest 'missing-binaries' {
    param($document)
    $document.PSObject.Properties.Remove('binaries')
}
$missingBinariesResult = Invoke-GuardMatrix -ManifestPath $missingBinariesPath -Name 'missing-binaries' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBinariesResult) -match 'binaries') `
    'a manifest without the binaries object was not reported'

$missingBinaryPath = Copy-And-MutateManifest 'missing-binary' {
    param($document)
    $document.binaries.PSObject.Properties.Remove('plugin')
}
$missingBinaryResult = Invoke-GuardMatrix -ManifestPath $missingBinaryPath -Name 'missing-binary' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBinaryResult) -match 'binary identity') `
    'a manifest without one binary identity was not reported'

$entryFramePath = Copy-And-MutateManifest 'entry-frame' {
    param($document)
    $document.entries[0].requested_capture_frame = 9
}
$entryFrameResult = Invoke-GuardMatrix -ManifestPath $entryFramePath -Name 'entry-frame' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $entryFrameResult) -match 'capture frame') `
    'an entry frame that disagrees with the manifest capture frame was not reported'

$authoredOpenglEntry = $baseManifest.entries |
    Where-Object { $_.host -eq 'runtime' -and $_.api -eq 'opengl' -and $_.case -eq 'authored-maps' } |
    Select-Object -First 1
$emptyOpenglEntry = $baseManifest.entries |
    Where-Object { $_.host -eq 'runtime' -and $_.api -eq 'opengl' -and $_.case -eq 'empty-image-bytes' } |
    Select-Object -First 1
$absentOpenglEntry = $baseManifest.entries |
    Where-Object { $_.host -eq 'runtime' -and $_.api -eq 'opengl' -and $_.case -eq 'absent-maps' } |
    Select-Object -First 1
$authoredVulkanEntry = $baseManifest.entries |
    Where-Object { $_.host -eq 'runtime' -and $_.api -eq 'vulkan' -and $_.case -eq 'authored-maps' } |
    Select-Object -First 1
$emptyEditorEntry = $baseManifest.entries |
    Where-Object { $_.host -eq 'editor' -and $_.api -eq 'd3d12' -and $_.case -eq 'empty-image-bytes' } |
    Select-Object -First 1

$pristine = @{}
foreach ($entry in @(
        $authoredOpenglEntry, $emptyOpenglEntry, $absentOpenglEntry,
        $authoredVulkanEntry, $emptyEditorEntry)) {
    $pristine[$entry.handshake] = [System.IO.File]::ReadAllBytes($entry.handshake)
    $pristine[$entry.png] = [System.IO.File]::ReadAllBytes($entry.png)
    $pristine[$entry.stdout] = [System.IO.File]::ReadAllBytes($entry.stdout)
}
function Restore-FixtureFile([string]$Path) {
    if ($pristine.ContainsKey($Path)) {
        [System.IO.File]::WriteAllBytes($Path, $pristine[$Path])
    }
}
function Set-FixtureHandshake([string]$Path, [scriptblock]$Mutation) {
    $document = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    & $Mutation $document
    Write-FixtureJson $Path $document
}
function Invoke-FixtureSynth(
    [string]$PngPath,
    [string]$HandshakePath,
    [string]$HostName,
    [string]$Api,
    [string]$CaseName,
    [string]$Defect) {
    return Invoke-FixtureProcess `
        -FilePath $guardPath `
        -Arguments @(
            'synth',
            "--out-png=$PngPath",
            "--out-handshake=$HandshakePath",
            "--host=$HostName",
            "--api=$Api",
            "--case=$CaseName",
            "--defect=$Defect",
            '--surface=320x180') `
        -TimeoutSeconds 60
}

# A capture frame that disagrees with the raw screenshot log must be rejected.
$frameMismatchPath = Copy-And-MutateManifest 'frame-mismatch' {
    param($document)
}
$frameText = [System.IO.File]::ReadAllText($authoredOpenglEntry.stdout)
$frameText = $frameText -replace 'frame=4', 'frame=3'
[System.IO.File]::WriteAllText($authoredOpenglEntry.stdout, $frameText)
$frameMismatchResult = Invoke-GuardMatrix -ManifestPath $frameMismatchPath -Name 'frame-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $frameMismatchResult) -match 'capture frame|screenshot frame') `
    'capture frame mismatch was not reported'
Restore-FixtureFile $authoredOpenglEntry.stdout

# Wrong backend and wrong host for a real handshake.
$backendMismatchPath = Copy-And-MutateManifest 'backend-mismatch' {
    param($document)
}
Set-FixtureHandshake $authoredVulkanEntry.handshake {
    param($handshake)
    $handshake.actual_backend = 'opengl'
}
$backendMismatchResult = Invoke-GuardMatrix -ManifestPath $backendMismatchPath -Name 'backend-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $backendMismatchResult) -match 'actual backend|requested') `
    'actual backend mismatch was not reported'
Restore-FixtureFile $authoredVulkanEntry.handshake

$hostPath = Copy-And-MutateManifest 'host-mismatch' {
    param($document)
}
Set-FixtureHandshake $emptyEditorEntry.handshake {
    param($handshake)
    $handshake.host = 'runtime'
}
$hostResult = Invoke-GuardMatrix -ManifestPath $hostPath -Name 'host-mismatch' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $hostResult) -match 'host') `
    'handshake host mismatch was not reported'
Restore-FixtureFile $emptyEditorEntry.handshake

$missingBackendPath = Copy-And-MutateManifest 'missing-requested-backend' {
    param($document)
}
Set-FixtureHandshake $authoredOpenglEntry.handshake {
    param($handshake)
    $handshake.PSObject.Properties.Remove('requested_backend')
}
$missingBackendResult = Invoke-GuardMatrix -ManifestPath $missingBackendPath -Name 'missing-requested-backend' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingBackendResult) -match 'requested backend') `
    'a missing requested backend was not reported'
Restore-FixtureFile $authoredOpenglEntry.handshake

$logicalFramePath = Copy-And-MutateManifest 'logical-frame' {
    param($document)
}
Set-FixtureHandshake $absentOpenglEntry.handshake {
    param($handshake)
    $handshake.logical_frame = 99
}
$logicalFrameResult = Invoke-GuardMatrix -ManifestPath $logicalFramePath -Name 'logical-frame' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $logicalFrameResult) -match 'logical frame') `
    'handshake logical frame mismatch was not reported'
Restore-FixtureFile $absentOpenglEntry.handshake

$colorSpacePath = Copy-And-MutateManifest 'color-space' {
    param($document)
}
Set-FixtureHandshake $authoredOpenglEntry.handshake {
    param($handshake)
    ($handshake.roles | Where-Object { $_.role -eq 'normal' }).texture_srgb = 1
}
$colorSpaceResult = Invoke-GuardMatrix -ManifestPath $colorSpacePath -Name 'color-space' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $colorSpaceResult) -match 'color space') `
    'a wrong role color-space flag was not reported'
Restore-FixtureFile $authoredOpenglEntry.handshake

$originPath = Copy-And-MutateManifest 'role-origin' {
    param($document)
}
Set-FixtureHandshake $emptyOpenglEntry.handshake {
    param($handshake)
    ($handshake.roles | Where-Object { $_.role -eq 'baseColor' }).origin = 'authored-map'
}
$originResult = Invoke-GuardMatrix -ManifestPath $originPath -Name 'role-origin' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $originResult) -match 'origin') `
    'a wrong role origin was not reported'
Restore-FixtureFile $emptyOpenglEntry.handshake

$debugViewPath = Copy-And-MutateManifest 'debug-view' {
    param($document)
}
Set-FixtureHandshake $authoredOpenglEntry.handshake {
    param($handshake)
    ($handshake.patches | Where-Object { $_.patch -eq 'roughness' }).debug_view = 5
}
$debugViewResult = Invoke-GuardMatrix -ManifestPath $debugViewPath -Name 'debug-view' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $debugViewResult) -match 'debug view') `
    'a wrong material debug view was not reported'
Restore-FixtureFile $authoredOpenglEntry.handshake

$assetPath = Copy-And-MutateManifest 'asset-directory' {
    param($document)
}
Set-FixtureHandshake $authoredOpenglEntry.handshake {
    param($handshake)
    $handshake.asset_directory = 'D:/Projects/Phlosion/PhlosionEngine/examples/material_fixture'
}
$assetResult = Invoke-GuardMatrix -ManifestPath $assetPath -Name 'asset-directory' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $assetResult) -match 'temporary directory') `
    'a non run-owned asset directory was not reported'
Restore-FixtureFile $authoredOpenglEntry.handshake

# Content defects. Each one is restored inside its own scope so a later
# negative cannot pass because an earlier mutation was left behind.
$contentDefects = @(
    @{ Entry = $authoredOpenglEntry; Defect = 'blank'; Expect = 'background' },
    @{ Entry = $authoredOpenglEntry; Defect = 'missing-patch'; Expect = 'occlusion' },
    @{ Entry = $authoredOpenglEntry; Defect = 'swapped-metallic-roughness'; Expect = 'roughness|metallic|channel' },
    @{ Entry = $authoredOpenglEntry; Defect = 'neutralized-normal'; Expect = 'normal' },
    @{ Entry = $authoredOpenglEntry; Defect = 'neutralized-ao'; Expect = 'occlusion' },
    @{ Entry = $authoredOpenglEntry; Defect = 'neutralized-emissive'; Expect = 'emissive' },
    @{ Entry = $authoredOpenglEntry; Defect = 'missing-lit-region'; Expect = 'lit' },
    @{ Entry = $authoredOpenglEntry; Defect = 'wrong-color-space'; Expect = 'color-space|treatment' },
    @{ Entry = $emptyOpenglEntry; Defect = 'old-black-fallback'; Expect = 'normal|placeholder' },
    @{ Entry = $emptyEditorEntry; Defect = 'old-black-fallback'; Expect = 'normal|placeholder' },
    @{ Entry = $authoredVulkanEntry; Defect = 'wrong-color-space'; Expect = 'color-space|treatment' }
    @{ Entry = $authoredOpenglEntry; Defect = 'same-mean-corruption'; Expect = 'interior|tolerance' }
    @{ Entry = $emptyOpenglEntry; Defect = 'same-mean-corruption'; Expect = 'interior|tolerance' }
    @{ Entry = $emptyOpenglEntry; Defect = 'frozen-lit-across-cases'; Expect = 'frozen|lit' }
    @{ Entry = $absentOpenglEntry; Defect = 'frozen-lit-across-cases'; Expect = 'frozen|lit' }
)
$defectIndex = 0
foreach ($defect in $contentDefects) {
    $defectIndex++
    $name = "content-$defectIndex-$($defect.Defect)"
    $caseName = $defect.Entry.case
    try {
        $synthResult = Invoke-FixtureSynth `
            -PngPath $defect.Entry.png `
            -HandshakePath $defect.Entry.handshake `
            -HostName $defect.Entry.host `
            -Api $defect.Entry.api `
            -CaseName $caseName `
            -Defect $defect.Defect
        Check-Fixture ($synthResult.ExitCode -eq 0) "$name synth failed: $($synthResult.Stderr)"
        $path = Copy-And-MutateManifest $name {
            param($document)
        }
        $result = Invoke-GuardMatrix -ManifestPath $path -Name $name -ExpectSuccess $false
        Check-Fixture ((Get-GuardFailureText $result) -match $defect.Expect) `
            "$($defect.Defect) on $caseName was not reported"
    } finally {
        Restore-FixtureFile $defect.Entry.png
        Restore-FixtureFile $defect.Entry.handshake
    }
}

$runDirectory = Join-Path $workRoot 'run-owned'
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

# Path containment: a traversal, a sibling whose name merely shares a prefix and
# an entry path outside the run directory are all rejected.
$traversalPath = Copy-And-MutateManifest 'traversal' {
    param($document)
    $document.entries[0].png = (Join-Path $workRoot '..\outside.png')
}
$traversalResult = Invoke-GuardMatrix -ManifestPath $traversalPath -Name 'traversal' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $traversalResult) -match 'outside the run directory|not a valid run path') `
    'a traversal entry path was not reported'

$siblingPath = Copy-And-MutateManifest 'sibling-prefix' {
    param($document)
    $document.entries[1].handshake = ($workRoot + '-sibling\handshake.json')
}
$siblingResult = Invoke-GuardMatrix -ManifestPath $siblingPath -Name 'sibling-prefix' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $siblingResult) -match 'outside the run directory|not a valid run path') `
    'a sibling prefix path was not reported'

# Outside output files must be refused before they are created, and a sentinel at
# the target location must survive untouched.
$outsideOut = $workRoot + '-outside-matrix.json'
$outsideSheet = $workRoot + '-outside-sheet.png'
[System.IO.File]::WriteAllText($outsideOut, 'sentinel')
[System.IO.File]::WriteAllText($outsideSheet, 'sentinel')
$outsideResult = Invoke-GuardMatrix `
    -ManifestPath $baseManifestPath `
    -Name 'outside-output' `
    -ExpectSuccess $false `
    -OutPath $outsideOut `
    -SheetPath $outsideSheet
Check-Fixture ((Get-GuardFailureText $outsideResult) -match 'not a valid run path|outside the run directory') `
    'outside output paths were not reported'
Check-Fixture ([System.IO.File]::ReadAllText($outsideOut) -eq 'sentinel') `
    'an outside --out file was written'
Check-Fixture ([System.IO.File]::ReadAllText($outsideSheet) -eq 'sentinel') `
    'an outside --sheet file was written'

# Required editor evidence: a missing metrics path, a missing required field and
# mismatched recorded evidence are all rejected.
$missingMetricsPath = Copy-And-MutateManifest 'missing-metrics-field' {
    param($document)
    foreach ($entry in $document.entries) {
        if ($entry.host -eq 'editor') {
            $entry.PSObject.Properties.Remove('metrics')
        }
    }
}
$missingMetricsResult = Invoke-GuardMatrix -ManifestPath $missingMetricsPath -Name 'missing-metrics-field' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingMetricsResult) -match 'metrics') `
    'a missing editor metrics field was not reported'

$missingFieldPath = Copy-And-MutateManifest 'missing-field' {
    param($document)
    $document.entries[2].PSObject.Properties.Remove('handshake')
}
$missingFieldResult = Invoke-GuardMatrix -ManifestPath $missingFieldPath -Name 'missing-field' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $missingFieldResult) -match 'required field|handshake') `
    'a missing required entry field was not reported'

$recordedFramePath = Copy-And-MutateManifest 'recorded-frame' {
    param($document)
    $document.entries[0].observed_capture_frame = 3
}
$recordedFrameResult = Invoke-GuardMatrix -ManifestPath $recordedFramePath -Name 'recorded-frame' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $recordedFrameResult) -match 'observed capture frame') `
    'a recorded observed frame that disagrees with the request was not reported'

$recordedBackendPath = Copy-And-MutateManifest 'recorded-backend' {
    param($document)
    $document.entries[0].screenshot_backend = 'Vulkan'
}
$recordedBackendResult = Invoke-GuardMatrix -ManifestPath $recordedBackendPath -Name 'recorded-backend' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $recordedBackendResult) -match 'screenshot backend') `
    'a recorded screenshot backend that disagrees with the log was not reported'

$recordedSizePath = Copy-And-MutateManifest 'recorded-size' {
    param($document)
    $document.entries[1].screenshot_size = @(1, 1)
}
$recordedSizeResult = Invoke-GuardMatrix -ManifestPath $recordedSizePath -Name 'recorded-size' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $recordedSizeResult) -match 'screenshot size|log dimensions') `
    'a recorded screenshot size that disagrees with the PNG was not reported'

# Numeric evidence above the 32-bit range must be rejected before any narrowing
# cast: the correct width plus 2^32 and UINT64_MAX both previously truncated into
# an accepted size.
$sizeOverflowPath = Copy-And-MutateManifest 'recorded-size-overflow' {
    param($document)
    $document.entries[0].screenshot_size = @([int64](320 + 4294967296), 180)
}
$sizeOverflowResult = Invoke-GuardMatrix -ManifestPath $sizeOverflowPath -Name 'recorded-size-overflow' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $sizeOverflowResult) -match 'screenshot_size|bounded integer') `
    'a screenshot size above the 32-bit range was not reported'

$sizeMaxPath = Copy-And-MutateManifest 'recorded-size-uint64max' {
    param($document)
    $document.entries[0].screenshot_size = @([uint64]::MaxValue, 180)
}
$sizeMaxResult = Invoke-GuardMatrix -ManifestPath $sizeMaxPath -Name 'recorded-size-uint64max' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $sizeMaxResult) -match 'screenshot_size|bounded integer') `
    'a UINT64_MAX screenshot size was not reported'

$sizeHeightOverflowPath = Copy-And-MutateManifest 'recorded-size-height-overflow' {
    param($document)
    $document.entries[3].screenshot_size = @(320, [int64](180 + 4294967296))
}
$sizeHeightOverflowResult = Invoke-GuardMatrix -ManifestPath $sizeHeightOverflowPath -Name 'recorded-size-height-overflow' -ExpectSuccess $false
Check-Fixture ((Get-GuardFailureText $sizeHeightOverflowResult) -match 'screenshot_size|bounded integer') `
    'a screenshot height above the 32-bit range was not reported'

# Handshake type and identity hardening.
$handshakeNegatives = @(
    @{ Name = 'fractional-frame'; Expect = 'bounded integer|logical frame'; Mutate = {
            param($handshake)
            $handshake.logical_frame = 0.5
        } },
    @{ Name = 'oversized-surface'; Expect = 'bounded integer|surface'; Mutate = {
            param($handshake)
            $handshake.surface.width = 100000000
        } },
    @{ Name = 'unknown-patch'; Expect = 'patch'; Mutate = {
            param($handshake)
            $handshake.patches[0].patch = 'bogus'
        } },
    @{ Name = 'wrong-factor-type'; Expect = 'factor'; Mutate = {
            param($handshake)
            $handshake.material.roughness_factor = '0.8'
        } },
    @{ Name = 'invented-hash'; Expect = 'content hash'; Mutate = {
            param($handshake)
            ($handshake.roles | Where-Object { $_.role -eq 'normal' }).content_hash = 12345
        } },
    @{ Name = 'hash-plus-2pow32'; Expect = 'content hash'; Mutate = {
            param($handshake)
            $role = $handshake.roles | Where-Object { $_.role -eq 'normal' }
            $role.content_hash = [int64]$role.content_hash + 4294967296
        } },
    @{ Name = 'hash-uint64max'; Expect = 'content hash'; Mutate = {
            param($handshake)
            ($handshake.roles | Where-Object { $_.role -eq 'normal' }).content_hash = [uint64]::MaxValue
        } },
    @{ Name = 'invented-key'; Expect = 'key'; Mutate = {
            param($handshake)
            ($handshake.roles | Where-Object { $_.role -eq 'baseColor' }).key = 'fixture/mat/authored-maps/baseColor/999'
        } },
    @{ Name = 'wrong-first-pixel'; Expect = 'first pixel'; Mutate = {
            param($handshake)
            $handshake.roles[0].first_pixel = @(0, 0, 0, 0)
        } },
    @{ Name = 'missing-sampler'; Expect = 'filter|wrap'; Mutate = {
            param($handshake)
            ($handshake.roles | Where-Object { $_.role -eq 'occlusion' }).PSObject.Properties.Remove('mag_filter')
        } }
)
foreach ($negative in $handshakeNegatives) {
    $manifestPath = Copy-And-MutateManifest $negative.Name { param($document) }
    Set-FixtureHandshake $authoredOpenglEntry.handshake $negative.Mutate
    $result = Invoke-GuardMatrix -ManifestPath $manifestPath -Name $negative.Name -ExpectSuccess $false
    Check-Fixture ((Get-GuardFailureText $result) -match $negative.Expect) `
        "handshake negative '$($negative.Name)' was not reported"
    Restore-FixtureFile $authoredOpenglEntry.handshake
}

# After every targeted mutation is restored, the pristine matrix must pass
# again; otherwise a negative could have been caused by stale state.
$pristineAfterDefects = Invoke-GuardMatrix `
    -ManifestPath $baseManifestPath `
    -Name 'pristine-after-defects' `
    -ExpectSuccess $true
Check-Fixture ($pristineAfterDefects.ExitCode -eq 0) `
    'the pristine matrix did not pass after restoring the content defects'

# Native process helper: quoting, exit codes and bounded tree cleanup.
$quoteText = 'spaces & percent % caret ^ quote " backslash \ comma, paren( ) bracket[ ]'
$echoResult = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', "--text=$quoteText") `
    -TimeoutSeconds 30
Check-Fixture ($echoResult.ExitCode -eq 0) 'echo helper did not exit zero'
Check-Fixture ($echoResult.Stdout.Contains('[MaterialFixture][Echo] ' + $quoteText)) `
    'quoted argument was not delivered verbatim'

$exitResult = Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', '--text=exit', '--exit=7') `
    -TimeoutSeconds 30
Check-Fixture ($exitResult.ExitCode -eq 7) 'nonzero child exit code was not preserved'

$grandchildPath = Join-Path $workRoot 'grandchild.ps1'
$childPath = Join-Path $workRoot 'child.ps1'
$markerPath = Join-Path $workRoot 'marker.txt'
$grandchildScript = @'
param([string]$Marker)
Start-Sleep -Seconds 4
Set-Content -LiteralPath $Marker -Value alive
'@
$childScript = @'
param([string]$Marker,[string]$Grandchild)
$quotedGrandchild = '"' + $Grandchild + '"'
$quotedMarker = '"' + $Marker + '"'
Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$quotedGrandchild,'-Marker',$quotedMarker) -WindowStyle Hidden | Out-Null
Start-Sleep -Seconds 60
'@
[System.IO.File]::WriteAllText($grandchildPath, $grandchildScript)
[System.IO.File]::WriteAllText($childPath, $childScript)
$timeoutResult = Invoke-FixtureProcess `
    -FilePath 'powershell.exe' `
    -Arguments @(
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        $childPath,
        '-Marker',
        $markerPath,
        '-Grandchild',
        $grandchildPath) `
    -TimeoutSeconds 2
Check-Fixture ($timeoutResult.TimedOut) 'timeout test did not report a timeout'
Start-Sleep -Seconds 7
Check-Fixture (-not (Test-Path -LiteralPath $markerPath)) 'timed-out process tree was not cleaned up'
$lingering = @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" |
    Where-Object { $_.CommandLine -like "*$grandchildPath*" })
Check-Fixture ($lingering.Count -eq 0) 'grandchild process survived the timeout cleanup'

$sentinelName = 'PHLOSION_MATERIAL_FIXTURE_HARNESS_SENTINEL'
[Environment]::SetEnvironmentVariable($sentinelName, 'original', 'Process')
Invoke-FixtureProcess `
    -FilePath $guardPath `
    -Arguments @('echo', '--text=env') `
    -EnvironmentOverrides @{ PHLOSION_MATERIAL_FIXTURE_CASE = 'child-only' } `
    -TimeoutSeconds 30 | Out-Null
Check-Fixture (
    ([Environment]::GetEnvironmentVariable($sentinelName, 'Process')) -eq 'original') `
    'process launcher mutated the caller environment'
[Environment]::SetEnvironmentVariable($sentinelName, $null, 'Process')

if ($failures.Count -gt 0) {
    foreach ($failure in $failures) {
        Write-Host "[MaterialFixture][Harness] FAIL $failure"
    }
    Write-Host "[MaterialFixture][Harness] FAILED checks=$($failures.Count) work=$workRoot"
    exit 1
}
Write-Host "[MaterialFixture][Harness] PASS work=$workRoot"
exit 0
