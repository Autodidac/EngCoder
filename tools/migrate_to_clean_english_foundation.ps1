[CmdletBinding()]
param([string]$DataRoot=(Join-Path $env:LOCALAPPDATA 'ThreadSpire'),[switch]$Force)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
function Rel([string]$Base,[string]$Target){
  $b=[IO.Path]::GetFullPath($Base).TrimEnd('\')+'\'; $t=[IO.Path]::GetFullPath($Target)
  $bu=New-Object Uri($b); $tu=New-Object Uri($t)
  [Uri]::UnescapeDataString($bu.MakeRelativeUri($tu).ToString()).Replace('/','\')
}
function JsonAtomic([string]$Path,$Value){
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path)|Out-Null
  $tmp="$Path.tmp"; $Value|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $tmp -Encoding UTF8
  Move-Item -Force -LiteralPath $tmp -Destination $Path
}
if([string]::IsNullOrWhiteSpace($DataRoot)){throw 'No LOCALAPPDATA/DataRoot available.'}
$DataRoot=[IO.Path]::GetFullPath($DataRoot); $state=Join-Path $DataRoot 'state'
$migrations=Join-Path $state 'curriculum-migrations'; $marker=Join-Path $migrations 'clean-english-foundation-v4.json'
if((Test-Path -LiteralPath $marker)-and-not $Force){Write-Host '> Clean English migration already applied.';exit 0}
if(Get-Process -Name ThreadSpire -ErrorAction SilentlyContinue){throw 'Close ThreadSpire before migrating its model files.'}
New-Item -ItemType Directory -Force -Path $DataRoot,$state,$migrations|Out-Null
$stamp=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$quarantine=Join-Path (Join-Path $DataRoot 'quarantine') "legacy-mixed-$stamp"
New-Item -ItemType Directory -Force -Path $quarantine|Out-Null
$excluded=@('runtime','logs','quarantine','cache','downloads')
$weights=@('.pt','.pth','.ckpt','.safetensors','.npz','.onnx','.bin')
$statePattern='(?i)(internal[-_]?model|candidate|checkpoint|optimizer|training|progress|curriculum|generation|resume|gradient|rng)'
$modelDir='(?i)(model|internal|candidate|checkpoint|generation|weight)'; $moved=New-Object Collections.Generic.List[string]
foreach($f in @(Get-ChildItem -LiteralPath $DataRoot -File -Recurse -Force -ErrorAction SilentlyContinue)){
  $r=Rel $DataRoot $f.FullName; $top=($r -split '[\\/]')[0].ToLowerInvariant()
  if($excluded -contains $top){continue}
  $isWeight=$weights -contains $f.Extension.ToLowerInvariant()
  $isManifest=($f.Name -match '(?i)^(manifest|model[-_]?state|generation[-_]?state)\.json$')-and($f.DirectoryName -match $modelDir)
  $isState=($top -eq 'state')-and($f.Name -match $statePattern)
  $isLock=$f.Name -match '(?i)^(training\.lock|training\.paused|inference\.active)$'
  if(-not($isWeight-or$isManifest-or$isState-or$isLock)){continue}
  $dest=Join-Path $quarantine $r; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest)|Out-Null
  Move-Item -Force -LiteralPath $f.FullName -Destination $dest; $moved.Add($r)
}
$guard=[ordered]@{schema_version=1;migration='clean-english-foundation-v4';migrated_at_utc=(Get-Date).ToUniversalTime().ToString('o');selected_target='english-foundation';effective_weight_target='english-foundation';english_foundation_established=$false;clean_generation_required=$true;legacy_weights_reused=$false;legacy_quarantine=$quarantine;app_data_root=$DataRoot}
JsonAtomic (Join-Path $state 'english-foundation-guard.json') $guard
JsonAtomic $marker ([ordered]@{schema_version=1;completed_at_utc=(Get-Date).ToUniversalTime().ToString('o');moved_file_count=$moved.Count;quarantine=$quarantine;next_generation='fresh random weights; English Foundation only'})
$report=Join-Path $state 'MODEL_STORAGE.txt'
@('ThreadSpire model storage and lineage','=====================================',"Application data root: $DataRoot","Legacy/mixed quarantine: $quarantine","Moved files: $($moved.Count)",'Active lineage after startup: fresh random weights; English Foundation only','Legacy weights reused: no','C++ updates: blocked until English evaluation and establishment both pass','', 'Quarantined paths:')+@($moved|Sort-Object)|Set-Content -LiteralPath $report -Encoding UTF8
Write-Host "> Quarantined $($moved.Count) old model/candidate files."
Write-Host '> Next startup creates fresh English-Foundation-only weights.'
Write-Host "> Report: $report"
