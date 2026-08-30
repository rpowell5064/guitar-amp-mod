param([string]$batch, [string[]]$caps = @("Clean:0.25:0.5:0.3","Hard Rock:0.75:0.95:0.05","Heavy:1.0:0.85:0.8"), [switch]$nobuild, [string]$outDir = "$env:TEMP\namcmp_sweep", [string]$refDir = "build-tools/namcmp/b7k", [string]$refPrefix = "B7K ", [string]$model = "b7k")
# nam_compare batch sweep (capture-fit workflow, 2026-08-28). $batch = file with lines "label|extra nam_compare args"
# (e.g. "G1|--level 0.6 --fit 90,1,8,7,-4,9,3000,0,2.2"); $caps = "Name:gain[:mix[:tone]]" per capture
# (ref = "$refDir/$refPrefix$Name.nam"). Rebuilds nam_compare first (Smart App Control blocks stale exes ~1 min
# after linking — build + run must be one command). Full outputs land in $outDir; prints one specESR row per config.
$cm = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
Set-Location C:\Development\Projects\guitar-amp-mod
if (-not $nobuild) {
  (Get-Item tools\nam_compare.cpp).LastWriteTime = Get-Date
  $b = & $cm --build build-tools/out --config Release --target nam_compare 2>&1
  if ($b -match ' error ') { $b | Select-String ' error ' | Select-Object -First 10; exit 1 }
}
New-Item -ItemType Directory -Force $outDir | Out-Null
$tag = [IO.Path]::GetFileNameWithoutExtension($batch)
$lines = Get-Content $batch | Where-Object { $_ -and -not $_.StartsWith('#') }
foreach ($ln in $lines) {
  $label, $extra = $ln -split '\|', 2
  $row = "{0,-8}" -f $label; $sum = 0.0; $n = 0
  foreach ($cg in $caps) {
    $cap, $g, $mx, $tn = $cg -split ':'; $mixArg = @(); if ($mx) { $mixArg = @("--mix", $mx) }; if ($tn) { $mixArg += @("--tone", $tn) }
    $f = "$outDir\$tag`_$label`_$($cap -replace ' ','')_g$g.txt"
    $a = @("--ref","$refDir/$refPrefix$cap.nam","--model",$model,"--gain",$g,"--in","di_ref/di_all.wav") + $mixArg + ($extra -split ' ' | Where-Object { $_ })
    & .\build-tools\out\Release\nam_compare.exe @a 2>&1 | Out-File -Encoding utf8 $f
    $l = (Select-String -Path $f -Pattern 'specESR: ([0-9.]+)%' | Select-Object -First 1)
    if ($l) { $v = [double]$l.Matches[0].Groups[1].Value; $sum += $v; $n++ } else { $v = -1 }
    $row += "  {0,-9}{1,6:N2}" -f ($cap -replace ' ',''), $v
  }
  if ($n) { $row += "   mean {0,6:N2}" -f ($sum / $n) }
  $row
}

