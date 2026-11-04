# The training runs of a profile-guided build (make-dist.ps1 -Pgo): an instrumented
# program is run, headless, over what users run, and writes how often each branch and
# function ran next to its objects; the final build is laid out by those counts.
#
#   .\pgo-train.ps1 -App <staged folder> [-SysNext <System/Next>] [-Extra <folder>,...]
#
# Every built-in machine boots its own ROM; the Next, with System/Next as its card,
# boots NextZXOS and runs programs that come with it. -Extra adds every .nex found
# under the folders given (programs of one's own, run on the same card). The runs go
# one after another: the counts of each are added to the files at its exit.

param(
	[Parameter(Mandatory)] [string]$App,
	[string]$SysNext,
	[string[]]$Extra = @(),
	[int]$Frames = 400
)

$ErrorActionPreference = 'Stop'

$exe = Join-Path $App 'xpeccy-plus.exe'
if (-not (Test-Path $exe)) { throw "Not found: $exe" }
$work = Join-Path ([IO.Path]::GetTempPath()) "xpeccy-pgo-$PID"
$cfg = Join-Path $work 'config'
New-Item -ItemType Directory -Force $cfg | Out-Null

$cases = [System.Collections.Generic.List[string]]::new()
foreach ($m in 'zx48', 'zx128', 'zxplus3', 'pent', 'scorp', 'atm2', 'evo-tsconf', 'evo-baseconf', 'profi') {
	$cases.Add("-m $m --bench-skip 300")
}
$nex = @()
if ($SysNext) {
	if (-not (Test-Path $SysNext)) { throw "Not found: $SysNext" }
	# NextZXOS boots for some 1200 frames before a program starts
	$cases.Add("-m zxnext --sdcard `"$SysNext`" --bench-skip 1500")
	foreach ($p in 'demos\scrollnutter\ScrollNutter.nex', 'demos\Spectron 2084\Spectron2084.nex',
			'demos\tilemap\tm.nex', 'demos\WidescreenImageDemo\Daybreak.nex', 'games\Next\TX-1696\main.nex',
			'games\Next\Warhawk\Warhawk.nex', 'games\Next\Nextoid\nextoid.nex', 'games\Next\AngryBloaters\Bloaters.nex') {
		$f = Join-Path $SysNext $p
		if (Test-Path $f) { $nex += $f }
	}
	foreach ($d in $Extra) {
		if (-not (Test-Path $d)) { throw "Not found: $d" }
		$nex += Get-ChildItem $d -Recurse -File -Include '*.nex' | ForEach-Object FullName
	}
	foreach ($f in $nex) { $cases.Add("-m zxnext --sdcard `"$SysNext`" `"$f`" --bench-skip 1500") }
} elseif ($Extra) {
	Write-Host 'pgo: -Extra needs -SysNext (the card its programs run on)' -ForegroundColor Yellow
}

# with sound mixed, as a user runs it
$out = Join-Path $work 'run.out'
$n = 0
foreach ($a in $cases) {
	$n++
	$cl = "--confdir `"$cfg`" --no-update-check $a --bench $Frames --bench-full"
	$p = Start-Process $exe -ArgumentList $cl -NoNewWindow -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError "$out.err"
	$line = Get-Content $out | Select-String '^bench' | Select-Object -Last 1
	if (($p.ExitCode -ne 0) -or -not $line) { throw "pgo: run $n failed: $a" }
	Write-Host ("pgo {0,2}/{1}: {2}" -f $n, $cases.Count, ($line.Line -replace '^bench: ', ''))
}
Remove-Item $work -Recurse -Force
