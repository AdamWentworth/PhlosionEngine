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

$brandRoot = Join-Path $PSScriptRoot 'branding'
$lockupPath = Join-Path $brandRoot 'phlosion-lockup-horizontal-cream.png'
$iconPath = Join-Path $brandRoot 'phlosion-mark.ico'
foreach ($path in @($lockupPath, $iconPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing Phlosion brand asset: $path" }
}
$palette = @{
    Background = [System.Drawing.ColorTranslator]::FromHtml('#050b13')
    Header = [System.Drawing.ColorTranslator]::FromHtml('#081421')
    Surface = [System.Drawing.ColorTranslator]::FromHtml('#0b1826')
    Raised = [System.Drawing.ColorTranslator]::FromHtml('#14273a')
    Line = [System.Drawing.ColorTranslator]::FromHtml('#284059')
    Text = [System.Drawing.ColorTranslator]::FromHtml('#f7efd9')
    Secondary = [System.Drawing.ColorTranslator]::FromHtml('#c5d2df')
    Muted = [System.Drawing.ColorTranslator]::FromHtml('#91a3b5')
    Orange = [System.Drawing.ColorTranslator]::FromHtml('#ff5a00')
    OrangeHover = [System.Drawing.ColorTranslator]::FromHtml('#ff7b32')
    Amber = [System.Drawing.ColorTranslator]::FromHtml('#ffb41f')
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Phlosion Editor'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object System.Drawing.Size(760, 500)
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$form.BackColor = $palette.Background
$brandIcon = New-Object System.Drawing.Icon($iconPath)
$form.Icon = $brandIcon

$header = New-Object System.Windows.Forms.Panel
$header.BackColor = $palette.Header
$header.Location = New-Object System.Drawing.Point(0, 0)
$header.Size = New-Object System.Drawing.Size(760, 134)
$form.Controls.Add($header)

$lockup = New-Object System.Windows.Forms.PictureBox
$lockup.Image = [System.Drawing.Image]::FromFile($lockupPath)
$lockup.SizeMode = 'Zoom'
$lockup.BackColor = $palette.Header
$lockup.Location = New-Object System.Drawing.Point(30, 11)
$lockup.Size = New-Object System.Drawing.Size(267, 105)
$header.Controls.Add($lockup)

$editorLabel = New-Object System.Windows.Forms.Label
$editorLabel.Text = 'EDITOR'
$editorLabel.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 14)
$editorLabel.ForeColor = $palette.Orange
$editorLabel.BackColor = $palette.Header
$editorLabel.Location = New-Object System.Drawing.Point(312, 55)
$editorLabel.AutoSize = $true
$header.Controls.Add($editorLabel)

$headerLine = New-Object System.Windows.Forms.Panel
$headerLine.BackColor = $palette.Line
$headerLine.Location = New-Object System.Drawing.Point(0, 133)
$headerLine.Size = New-Object System.Drawing.Size(760, 1)
$header.Controls.Add($headerLine)

$heading = New-Object System.Windows.Forms.Label
$heading.Text = 'Choose a project'
$heading.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 20)
$heading.ForeColor = $palette.Text
$heading.Location = New-Object System.Drawing.Point(31, 151)
$heading.AutoSize = $true
$form.Controls.Add($heading)

$description = New-Object System.Windows.Forms.Label
$description.Text = 'Select a game to open its editor workspace.'
$description.ForeColor = $palette.Secondary
$description.Location = New-Object System.Drawing.Point(33, 195)
$description.AutoSize = $true
$form.Controls.Add($description)

$list = New-Object System.Windows.Forms.ListBox
$list.DrawMode = 'OwnerDrawFixed'
$list.ItemHeight = 72
$list.IntegralHeight = $false
$list.BorderStyle = 'None'
$list.BackColor = $palette.Background
$list.ForeColor = $palette.Text
$list.Location = New-Object System.Drawing.Point(28, 226)
$list.Size = New-Object System.Drawing.Size(704, 185)
$itemNameFont = New-Object System.Drawing.Font('Segoe UI Semibold', 11)
$itemPathFont = New-Object System.Drawing.Font('Segoe UI', 8.5)
foreach ($project in $projects) {
    [void]$list.Items.Add($project)
}
if ($list.Items.Count -gt 0) { $list.SelectedIndex = 0 }
$list.Add_DrawItem({
    param($sender, $event)
    if ($event.Index -lt 0) { return }
    $project = $sender.Items[$event.Index]
    $selected = ($event.State -band [System.Windows.Forms.DrawItemState]::Selected) -ne 0
    $background = New-Object System.Drawing.SolidBrush($(if ($selected) { $palette.Raised } else { $palette.Surface }))
    $card = New-Object System.Drawing.Rectangle(($event.Bounds.X + 5), ($event.Bounds.Y + 3), ($event.Bounds.Width - 10), ($event.Bounds.Height - 6))
    $event.Graphics.FillRectangle($background, $card)
    $background.Dispose()
    $border = New-Object System.Drawing.Pen($(if ($selected) { $palette.Orange } else { $palette.Line }))
    $event.Graphics.DrawRectangle($border, $card)
    $border.Dispose()
    if ($selected) {
        $accent = New-Object System.Drawing.SolidBrush($palette.Orange)
        $event.Graphics.FillRectangle($accent, $card.X, $card.Y, 4, $card.Height)
        $accent.Dispose()
    }
    $nameRect = New-Object System.Drawing.Rectangle(($card.X + 20), ($card.Y + 11), ($card.Width - 36), 23)
    $pathRect = New-Object System.Drawing.Rectangle(($card.X + 20), ($card.Y + 38), ($card.Width - 36), 20)
    $flags = [System.Windows.Forms.TextFormatFlags]::Left -bor [System.Windows.Forms.TextFormatFlags]::EndEllipsis -bor [System.Windows.Forms.TextFormatFlags]::NoPrefix
    [System.Windows.Forms.TextRenderer]::DrawText($event.Graphics, $project.Name, $itemNameFont, $nameRect, $palette.Text, $flags)
    [System.Windows.Forms.TextRenderer]::DrawText($event.Graphics, (Split-Path -Parent $project.Descriptor), $itemPathFont, $pathRect, $palette.Muted, $flags)
})
$form.Controls.Add($list)

$footerLine = New-Object System.Windows.Forms.Panel
$footerLine.BackColor = $palette.Line
$footerLine.Location = New-Object System.Drawing.Point(0, 427)
$footerLine.Size = New-Object System.Drawing.Size(760, 1)
$form.Controls.Add($footerLine)

$status = New-Object System.Windows.Forms.Label
$status.Text = if ($projects.Count -gt 0) {
    'Game projects in your Games folder appear here automatically.'
} else {
    'Browse to a phlosion.project.json to open a project.'
}
$status.ForeColor = $palette.Muted
$status.Location = New-Object System.Drawing.Point(32, 453)
$status.Size = New-Object System.Drawing.Size(430, 28)
$form.Controls.Add($status)

$openButton = New-Object System.Windows.Forms.Button
$openButton.Text = 'Open Project'
$openButton.FlatStyle = 'Flat'
$openButton.FlatAppearance.BorderSize = 0
$openButton.FlatAppearance.MouseOverBackColor = $palette.OrangeHover
$openButton.BackColor = $palette.Orange
$openButton.ForeColor = $palette.Background
$openButton.Font = New-Object System.Drawing.Font('Segoe UI Semibold', 10)
$openButton.Location = New-Object System.Drawing.Point(610, 446)
$openButton.Size = New-Object System.Drawing.Size(124, 36)
$form.Controls.Add($openButton)
$form.AcceptButton = $openButton

$browseButton = New-Object System.Windows.Forms.Button
$browseButton.Text = 'Browse...'
$browseButton.FlatStyle = 'Flat'
$browseButton.FlatAppearance.BorderColor = $palette.Line
$browseButton.FlatAppearance.MouseOverBackColor = $palette.Raised
$browseButton.BackColor = $palette.Surface
$browseButton.ForeColor = $palette.Text
$browseButton.Location = New-Object System.Drawing.Point(496, 446)
$browseButton.Size = New-Object System.Drawing.Size(104, 36)
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
            $setup = Join-Path $PSScriptRoot 'Run-ProjectSetup.ps1'
            $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                "`"$setup`"", '-Launcher', "`"$($Project.Launcher)`"",
                '-LogBase', "`"$logBase`"")
            if ((Get-Command -Name $Project.Launcher).Parameters.ContainsKey('Detached')) {
                $arguments += '-Detached'
            }
            # Windows PowerShell loses ExitCode on Start-Process with redirected streams.
            $script:launchProcess = Start-Process -FilePath 'powershell.exe' `
                -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $Project.Descriptor) `
                -WindowStyle Hidden -PassThru
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
    if ($null -ne $exitCode -and $exitCode -eq 0) { $form.Close(); return }
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
    $exitDescription = if ($null -eq $exitCode) { 'without a readable exit code' } else { "with code $exitCode" }
    [void][System.Windows.Forms.MessageBox]::Show(
        "Project setup exited $exitDescription.`n$detail`nLogs: $script:launchLog", 'Phlosion Editor')
})

$openProject = {
    if ($list.SelectedIndex -ge 0) {
        Start-ProjectEditor -Project $list.SelectedItem
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
$lockup.Image.Dispose()
$brandIcon.Dispose()
$itemNameFont.Dispose()
$itemPathFont.Dispose()
$form.Dispose()
