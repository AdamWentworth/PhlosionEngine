[CmdletBinding()]
param(
    [switch]$ListProjects,
    [switch]$SmokeTest
)

$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$gamesRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot '../../Games'))

function Get-PhlosionProject {
    param([Parameter(Mandatory = $true)][string]$DescriptorPath)

    $descriptor = Get-Content -LiteralPath $DescriptorPath -Raw | ConvertFrom-Json
    if (-not $descriptor.project_id -or -not $descriptor.display_name) {
        throw "Invalid Phlosion project descriptor: $DescriptorPath"
    }
    $projectRoot = Split-Path -Parent $DescriptorPath
    $launcher = @(
        'tools/Launch-Editor.ps1',
        'tools/launch_editor.ps1'
    ) | ForEach-Object { Join-Path $projectRoot $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    [pscustomobject]@{
        Name = [string]$descriptor.display_name
        ProjectId = [string]$descriptor.project_id
        Descriptor = [IO.Path]::GetFullPath($DescriptorPath)
        Launcher = $launcher
    }
}

$projects = @(
    if (Test-Path -LiteralPath $gamesRoot -PathType Container) {
        Get-ChildItem -LiteralPath $gamesRoot -Directory |
            ForEach-Object {
                $descriptor = Join-Path $_.FullName 'phlosion.project.json'
                if (Test-Path -LiteralPath $descriptor -PathType Leaf) {
                    Get-PhlosionProject -DescriptorPath $descriptor
                }
            } | Sort-Object Name
    }
)
if ($ListProjects) {
    ConvertTo-Json -InputObject $projects -Depth 3
    return
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Phlosion Editor'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object System.Drawing.Size(680, 390)
$form.MinimumSize = New-Object System.Drawing.Size(580, 340)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)

$heading = New-Object System.Windows.Forms.Label
$heading.Text = 'Choose a project'
$heading.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 18)
$heading.Location = New-Object System.Drawing.Point(24, 20)
$heading.AutoSize = $true
$form.Controls.Add($heading)

$description = New-Object System.Windows.Forms.Label
$description.Text = 'Open a Phlosion game project to start its editor.'
$description.Location = New-Object System.Drawing.Point(26, 62)
$description.AutoSize = $true
$form.Controls.Add($description)

$list = New-Object System.Windows.Forms.ListView
$list.View = 'Details'
$list.FullRowSelect = $true
$list.HideSelection = $false
$list.MultiSelect = $false
$list.Anchor = 'Top,Bottom,Left,Right'
$list.Location = New-Object System.Drawing.Point(24, 98)
$list.Size = New-Object System.Drawing.Size(632, 216)
[void]$list.Columns.Add('Project', 215)
[void]$list.Columns.Add('Location', 392)
foreach ($project in $projects) {
    $item = New-Object System.Windows.Forms.ListViewItem($project.Name)
    [void]$item.SubItems.Add((Split-Path -Parent $project.Descriptor))
    $item.Tag = $project
    [void]$list.Items.Add($item)
}
if ($list.Items.Count -gt 0) { $list.Items[0].Selected = $true }
$form.Controls.Add($list)

$status = New-Object System.Windows.Forms.Label
$status.Text = if ($projects.Count -gt 0) {
    'Projects in the Games folder appear here automatically.'
} else {
    'Browse to a phlosion.project.json to open a project.'
}
$status.Anchor = 'Bottom,Left,Right'
$status.Location = New-Object System.Drawing.Point(25, 326)
$status.Size = New-Object System.Drawing.Size(390, 25)
$form.Controls.Add($status)

$openButton = New-Object System.Windows.Forms.Button
$openButton.Text = 'Open Project'
$openButton.Anchor = 'Bottom,Right'
$openButton.Location = New-Object System.Drawing.Point(530, 324)
$openButton.Size = New-Object System.Drawing.Size(126, 32)
$form.Controls.Add($openButton)
$form.AcceptButton = $openButton

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = 'Browse...'
$browseButton.Anchor = 'Bottom,Right'
$browseButton.Location = New-Object System.Drawing.Point(416, 324)
$browseButton.Size = New-Object System.Drawing.Size(104, 32)
$form.Controls.Add($browseButton)

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 250
$script:launchProcess = $null
$script:launchLog = $null
$script:launchStarted = $null
$script:launchName = $null

function Start-ProjectEditor {
    param([Parameter(Mandatory = $true)]$Project)

    if ($script:launchProcess -and -not $script:launchProcess.HasExited) { return }
    $openButton.Enabled = $false
    $browseButton.Enabled = $false
    $list.Enabled = $false
    $status.Text = "Preparing $($Project.Name)..."
    try {
        if ($Project.Launcher) {
            $logDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'PhlosionEditor/launcher'
            New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
            $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
            $logBase = Join-Path $logDirectory "$($Project.ProjectId)-$stamp"
            $script:launchLog = $logBase
            $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$($Project.Launcher)`"")
            if ((Get-Command -Name $Project.Launcher).Parameters.ContainsKey('Detached')) {
                $arguments += '-Detached'
            }
            $script:launchProcess = Start-Process -FilePath 'powershell.exe' `
                -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $Project.Descriptor) `
                -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput "$logBase.out.log" `
                -RedirectStandardError "$logBase.err.log"
            $script:launchStarted = [DateTime]::UtcNow
            $script:launchName = $Project.Name
            $timer.Start()
            return
        }

        $editor = Join-Path $engineRoot 'build/RelWithDebInfo/PhlosionEditor.exe'
        if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
            throw "Build the Engine editor first: $editor"
        }
        Start-Process -FilePath $editor -WorkingDirectory (Split-Path -Parent $Project.Descriptor) `
            -ArgumentList "`"--project=$($Project.Descriptor)`""
        $form.Close()
    } catch {
        $openButton.Enabled = $true
        $browseButton.Enabled = $true
        $list.Enabled = $true
        $status.Text = 'Could not start the editor.'
        [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Phlosion Editor')
    }
}

$timer.Add_Tick({
    if (-not $script:launchProcess) { return }
    if (-not $script:launchProcess.HasExited) {
        $seconds = [int]([DateTime]::UtcNow - $script:launchStarted).TotalSeconds
        $status.Text = "Preparing $script:launchName... ${seconds}s"
        return
    }
    $timer.Stop()
    $exitCode = $script:launchProcess.ExitCode
    $script:launchProcess.Dispose()
    $script:launchProcess = $null
    if ($exitCode -eq 0) { $form.Close(); return }
    $errorLog = "$script:launchLog.err.log"
    $outputLog = "$script:launchLog.out.log"
    $detail = if (Test-Path -LiteralPath $errorLog) {
        @(Get-Content -LiteralPath $errorLog -Tail 12) -join [Environment]::NewLine
    } else { '' }
    if (-not $detail -and (Test-Path -LiteralPath $outputLog)) {
        $detail = @(Get-Content -LiteralPath $outputLog -Tail 12) -join [Environment]::NewLine
    }
    $openButton.Enabled = $true
    $browseButton.Enabled = $true
    $list.Enabled = $true
    $status.Text = 'Could not start the editor.'
    [void][System.Windows.Forms.MessageBox]::Show(
        "Project setup exited $exitCode.`n$detail`nLogs: $script:launchLog", 'Phlosion Editor')
})

$openProject = {
    if ($list.SelectedItems.Count -gt 0) {
        Start-ProjectEditor -Project $list.SelectedItems[0].Tag
    }
}
$openButton.Add_Click($openProject)
$list.Add_DoubleClick($openProject)
$browseButton.Add_Click({
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Open Phlosion Project'
    $dialog.Filter = 'Phlosion project (phlosion.project.json)|phlosion.project.json|JSON files (*.json)|*.json'
    $dialog.FileName = 'phlosion.project.json'
    if ($dialog.ShowDialog($form) -eq [System.Windows.Forms.DialogResult]::OK) {
        try { Start-ProjectEditor -Project (Get-PhlosionProject -DescriptorPath $dialog.FileName) }
        catch { [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Phlosion Editor') }
    }
    $dialog.Dispose()
})
if ($SmokeTest) {
    $timer.Add_Tick({ $timer.Stop(); $form.Close() })
    $timer.Start()
}
[void]$form.ShowDialog()
$timer.Dispose()
$form.Dispose()
