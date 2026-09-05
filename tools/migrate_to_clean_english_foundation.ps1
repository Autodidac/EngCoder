[CmdletBinding()]
param([string]$DataRoot=(Join-Path $env:LOCALAPPDATA 'EngCoder'),[switch]$Force)
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
$migrations=Join-Path $state 'curriculum-migrations'; $marker=Join-Path $migrations 'full-backbone-english-v6.json'
if((Test-Path -LiteralPath $marker)-and-not $Force){Write-Host '> Full-backbone English migration already applied.';exit 0}
if(Get-Process -Name EngCoder,ThreadSpire -ErrorAction SilentlyContinue){throw 'Close EngCoder before migrating its model files.'}
New-Item -ItemType Directory -Force -Path $DataRoot,$state,$migrations|Out-Null
$stamp=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$quarantine=Join-Path (Join-Path $DataRoot 'quarantine') "partial-backbone-$stamp"
New-Item -ItemType Directory -Force -Path $quarantine|Out-Null
$excluded=@('runtime','logs','quarantine','cache','downloads')
$weights=@('.pt','.pth','.ckpt','.safetensors','.npz','.onnx','.bin')
$statePattern='(?i)(internal[-_]?model|candidate|checkpoint|optimizer|training|progress|curriculum|generation|resume|gradient|rng)'
$modelDir='(?i)(model|internal|candidate|checkpoint|generation|weight)'; $moved=New-Object Collections.Generic.List[string]
$resetRoots=@('models\internal','candidates','jobs','experts')
foreach($relative in $resetRoots){
  $source=Join-Path $DataRoot $relative
  if(-not(Test-Path -LiteralPath $source)){continue}
  $destination=Join-Path $quarantine $relative
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination)|Out-Null
  Move-Item -Force -LiteralPath $source -Destination $destination
  $moved.Add("$relative\")
}
foreach($f in @(Get-ChildItem -LiteralPath $DataRoot -File -Recurse -Force -ErrorAction SilentlyContinue)){
  $r=Rel $DataRoot $f.FullName; $top=($r -split '[\\/]')[0].ToLowerInvariant()
  if($excluded -contains $top){continue}
  $isWeight=$weights -contains $f.Extension.ToLowerInvariant()
  $isManifest=($f.Name -match '(?i)^(manifest|model[-_]?state|generation[-_]?state)\.json$')-and($f.DirectoryName -match $modelDir)
  $isState=($top -eq 'state')-and($f.Name -match $statePattern)
  $isLock=($top -eq 'control')-and($f.Name -match '(?i)^(training|inference)\.')
  if(-not($isWeight-or$isManifest-or$isState-or$isLock)){continue}
  $dest=Join-Path $quarantine $r; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest)|Out-Null
  Move-Item -Force -LiteralPath $f.FullName -Destination $dest; $moved.Add($r)
}
$guard=[ordered]@{schema_version=2;migration='verified-hybrid-english-v7';bootstrap_policy='verified-hybrid-conversation-v7';migrated_at_utc=(Get-Date).ToUniversalTime().ToString('o');selected_target='english-foundation';training_data_scope='english-core';optimizer_scope='all';english_foundation_established=$false;full_backbone_trained=$false;generation_ready=$false;clean_generation_required=$true;legacy_weights_reused=$false;legacy_quarantine=$quarantine;app_data_root=$DataRoot}
JsonAtomic (Join-Path $state 'english-foundation-guard.json') $guard
JsonAtomic $marker ([ordered]@{schema_version=2;completed_at_utc=(Get-Date).ToUniversalTime().ToString('o');moved_file_count=$moved.Count;quarantine=$quarantine;next_generation='fresh random weights; English data trains the full backbone and must pass generation canaries'})
$report=Join-Path $state 'MODEL_STORAGE.txt'
@('EngCoder model storage and lineage','=====================================',"Application data root: $DataRoot","Partial-backbone quarantine: $quarantine","Moved roots/files: $($moved.Count)",'Active lineage after startup: fresh random weights; English data trains every shared and routed parameter','Bootstrap readiness: held-out loss plus deterministic generation canaries','Verified episode memory and user configuration preserved: yes','Legacy weights reused: no','Specialist updates: blocked until English full-backbone readiness passes','', 'Quarantined paths:')+@($moved|Sort-Object)|Set-Content -LiteralPath $report -Encoding UTF8
Write-Host "> Quarantined $($moved.Count) partial-backbone model/candidate roots and files."
Write-Host '> Next startup creates fresh weights and trains the full backbone on English Foundation data.'
Write-Host "> Report: $report"
