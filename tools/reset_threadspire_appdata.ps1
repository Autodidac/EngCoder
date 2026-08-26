[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [Parameter()]
    [string] $Target = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ThreadSpire'),

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $ReplacementVersion = '0.6.0',

    [Parameter()]
    [switch] $ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string] $Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-SamePath {
    param(
        [Parameter(Mandatory)][string] $Left,
        [Parameter(Mandatory)][string] $Right
    )
    return [string]::Equals(
        (Get-NormalizedPath -Path $Left),
        (Get-NormalizedPath -Path $Right),
        [StringComparison]::OrdinalIgnoreCase)
}

function Assert-ExactResetTarget {
    param(
        [Parameter(Mandatory)][string] $Candidate,
        [Parameter(Mandatory)][string] $LocalAppDataRoot
    )

    $localRoot = Get-NormalizedPath -Path $LocalAppDataRoot
    $resolved = Get-NormalizedPath -Path $Candidate
    $expected = Get-NormalizedPath -Path (Join-Path $localRoot 'ThreadSpire')
    if (-not (Test-SamePath -Left $resolved -Right $expected)) {
        throw "Reset target must be exactly '$expected'; received '$resolved'."
    }
    if (-not (Test-SamePath -Left ([IO.Path]::GetDirectoryName($resolved)) -Right $localRoot)) {
        throw 'Reset target parent is not the exact LocalAppData directory.'
    }
    if (-not [string]::Equals([IO.Path]::GetFileName($resolved), 'ThreadSpire', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reset target leaf must be ThreadSpire.'
    }

    $localItem = Get-Item -Force -LiteralPath $localRoot
    if (-not $localItem.PSIsContainer -or (($localItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw 'LocalAppData must be a real directory, not a link or reparse point.'
    }
    if (Test-Path -LiteralPath $resolved) {
        $targetItem = Get-Item -Force -LiteralPath $resolved
        if (-not $targetItem.PSIsContainer -or (($targetItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
            throw 'ThreadSpire reset target must be a real directory, not a link or reparse point.'
        }
    }
    return $resolved
}

function Get-TreeInventoryNoFollow {
    param([Parameter(Mandatory)][string] $Root)

    [uint64] $bytes = 0
    [uint64] $files = 0
    $links = [Collections.Generic.List[object]]::new()
    if (-not (Test-Path -LiteralPath $Root)) {
        return [pscustomobject]@{ Bytes = $bytes; Files = $files; Links = @() }
    }

    $rootPrefix = (Get-NormalizedPath -Path $Root) + [IO.Path]::DirectorySeparatorChar
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push((Get-NormalizedPath -Path $Root))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($entry in (Get-ChildItem -Force -LiteralPath $directory)) {
            $entryPath = Get-NormalizedPath -Path $entry.FullName
            if (-not $entryPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Inventory entry escaped reset root: $entryPath"
            }
            $isLink = (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            if ($isLink) {
                $links.Add([pscustomobject]@{
                    Path = [IO.Path]::GetRelativePath($Root, $entryPath)
                    Kind = if ($entry.PSIsContainer) { 'directory-link' } else { 'file-link' }
                })
                continue
            }
            if ($entry.PSIsContainer) {
                $pending.Push($entryPath)
            } else {
                $files += 1
                $bytes += [uint64]$entry.Length
            }
        }
    }
    return [pscustomobject]@{ Bytes = $bytes; Files = $files; Links = @($links) }
}

function Stop-ThreadSpireOwnedProcesses {
    param([Parameter(Mandatory)][string] $Root)

    $rootPrefix = (Get-NormalizedPath -Path $Root) + [IO.Path]::DirectorySeparatorChar
    $stopped = [Collections.Generic.List[object]]::new()
    foreach ($process in (Get-CimInstance Win32_Process)) {
        $executable = [string]$process.ExecutablePath
        $ownedByPath = -not [string]::IsNullOrWhiteSpace($executable) -and
            (Get-NormalizedPath -Path $executable).StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)
        $ownedByName =
            [string]::Equals([string]$process.Name, 'EngCoder.exe', [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals([string]$process.Name, 'ThreadSpire.exe', [StringComparison]::OrdinalIgnoreCase)
        if (-not ($ownedByPath -or $ownedByName)) {
            continue
        }
        $record = [pscustomobject]@{
            ProcessId = [uint32]$process.ProcessId
            Name = [string]$process.Name
            ExecutablePath = $executable
            Stopped = $false
        }
        try {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            Wait-Process -Id $process.ProcessId -Timeout 15 -ErrorAction SilentlyContinue
            $record.Stopped = -not [bool](Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue)
        } catch {
            throw "Unable to stop EngCoder-owned process $($process.ProcessId): $($_.Exception.Message)"
        }
        if (-not $record.Stopped) {
            throw "EngCoder-owned process $($process.ProcessId) did not stop within 15 seconds."
        }
        $stopped.Add($record)
    }
    return @($stopped)
}

function Remove-TreeNoFollow {
    param([Parameter(Mandatory)][string] $Root)

    if (-not (Test-Path -LiteralPath $Root)) {
        return
    }
    $rootPrefix = (Get-NormalizedPath -Path $Root) + [IO.Path]::DirectorySeparatorChar
    $directories = [Collections.Generic.List[string]]::new()
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push((Get-NormalizedPath -Path $Root))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        $directories.Add($directory)
        foreach ($entry in (Get-ChildItem -Force -LiteralPath $directory)) {
            $entryPath = Get-NormalizedPath -Path $entry.FullName
            if (-not $entryPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Deletion entry escaped reset root: $entryPath"
            }
            $isLink = (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
            if ($isLink) {
                if ($entry.PSIsContainer) {
                    [IO.Directory]::Delete($entryPath, $false)
                } else {
                    [IO.File]::Delete($entryPath)
                }
            } elseif ($entry.PSIsContainer) {
                $pending.Push($entryPath)
            } else {
                [IO.File]::SetAttributes($entryPath, [IO.FileAttributes]::Normal)
                [IO.File]::Delete($entryPath)
            }
        }
    }
    foreach ($directory in ($directories | Sort-Object { $_.Length } -Descending)) {
        [IO.Directory]::Delete($directory, $false)
    }
}

function Write-ManifestAtomically {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][object] $Value
    )
    $temporary = "$Path.tmp-$([Guid]::NewGuid().ToString('N'))"
    try {
        [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary, $Path, $true)
    } finally {
        if ([IO.File]::Exists($temporary)) {
            [IO.File]::Delete($temporary)
        }
    }
}

$localAppData = [Environment]::GetFolderPath('LocalApplicationData')
$resolvedTarget = Assert-ExactResetTarget -Candidate $Target -LocalAppDataRoot $localAppData
$validation = [pscustomobject]@{
    Valid = $true
    Target = $resolvedTarget
    ExpectedTarget = (Join-Path (Get-NormalizedPath -Path $localAppData) 'ThreadSpire')
    Exists = Test-Path -LiteralPath $resolvedTarget
}
if ($ValidateOnly) {
    $validation
    return
}
if (-not $PSCmdlet.ShouldProcess($resolvedTarget, 'Stop EngCoder processes, write reset manifest, and delete no-follow application data')) {
    return
}

$stoppedProcesses = Stop-ThreadSpireOwnedProcesses -Root $resolvedTarget
$resolvedTarget = Assert-ExactResetTarget -Candidate $resolvedTarget -LocalAppDataRoot $localAppData
$inventory = Get-TreeInventoryNoFollow -Root $resolvedTarget
$legacyVersion = $null
$legacyExecutable = Join-Path $resolvedTarget 'EngCoder.exe'
if (-not (Test-Path -LiteralPath $legacyExecutable -PathType Leaf)) {
    $legacyExecutable = Join-Path $resolvedTarget 'ThreadSpire.exe'
}
if (Test-Path -LiteralPath $legacyExecutable -PathType Leaf) {
    $legacyVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($legacyExecutable).FileVersion
}
$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$manifestPath = Join-Path (Get-NormalizedPath -Path $localAppData) "EngCoder-reset-$stamp-manifest.json"
$manifest = [ordered]@{
    Schema = 'threadspire.safe-reset.v1'
    Created = [DateTimeOffset]::Now.ToString('o')
    Target = $resolvedTarget
    ResolvedTarget = $resolvedTarget
    ReplacementVersion = $ReplacementVersion
    LegacyVersion = $legacyVersion
    Bytes = $inventory.Bytes
    Files = $inventory.Files
    LinkedEntriesNotFollowed = $inventory.Links
    StoppedProcesses = $stoppedProcesses
    DeletionScope = @('legacy runtime', 'models', 'accepted data', 'settings', 'queues', 'failed generations', 'outputs', 'caches')
    DeletionCompleted = $false
    DeletionError = $null
}
Write-ManifestAtomically -Path $manifestPath -Value $manifest

try {
    $resolvedTarget = Assert-ExactResetTarget -Candidate $resolvedTarget -LocalAppDataRoot $localAppData
    Remove-TreeNoFollow -Root $resolvedTarget
    $manifest.DeletionCompleted = -not (Test-Path -LiteralPath $resolvedTarget)
    if (-not $manifest.DeletionCompleted) {
        throw 'EngCoder legacy data directory still exists after reset traversal.'
    }
} catch {
    $manifest.DeletionError = $_.Exception.Message
    Write-ManifestAtomically -Path $manifestPath -Value $manifest
    throw
}
Write-ManifestAtomically -Path $manifestPath -Value $manifest
[pscustomobject]@{
    Target = $resolvedTarget
    Manifest = $manifestPath
    BytesDeleted = $inventory.Bytes
    FilesDeleted = $inventory.Files
    DeletionCompleted = $manifest.DeletionCompleted
}