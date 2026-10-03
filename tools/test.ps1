# kotemado の自動検証。画面は撮らず、検証用ウィンドウ(target.exe)が
# 終了時に書き出す自分の矩形・状態を、期待値と突き合わせる。
#
#   powershell -File tools\test.ps1 [-Work <作業フォルダ>]
#
# 検証中は小さな窓がいくつか画面に出ては消える。最大化の検証(D)だけは
# 窓が前面に出る(最大化はアクティブ化を伴うため)。
param([string]$Work = "$env:TEMP\kotemado-test")

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root 'kotemado.exe'
New-Item -ItemType Directory -Force $Work | Out-Null
Get-ChildItem $Work -Filter '*.txt' -ErrorAction SilentlyContinue | Remove-Item -Force
Remove-Item "$Work\test.log" -ErrorAction SilentlyContinue

# 検証用ウィンドウを作る
$target = Join-Path $Work 'target.exe'
& gcc -O2 -municode -mwindows -o $target (Join-Path $PSScriptRoot 'target.c') -ldwmapi
if ($LASTEXITCODE) { throw 'target.c のビルドに失敗' }
Copy-Item $target (Join-Path $Work 'ktm-m-target.exe') -Force

# 画面の大きさ(期待値の計算用)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace K -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(System.IntPtr v);
'@
[K.W]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null
$prim = [System.Windows.Forms.Screen]::PrimaryScreen
$mon  = $prim.Bounds; $wa = $prim.WorkingArea

$ini = @"
[general]
apply_existing=1

[rule]
name=A キャプションなし・絶対位置
class=exact:KtmA
title=exact:
display=1
x=100
y=200
width=800
height=600

[rule]
name=B 作業領域の中央に半分の大きさ
class=exact:KtmB
title=prefix:kotemado-B
display=main
base=work
x=center
y=center
width=50%
height=50%

[rule]
name=C 枠を含めた窓の矩形で合わせる
class=exact:KtmC
display=1
x=10
y=20
width=500
height=400
frame=window

[rule]
name=D 最大化と最前面
class=exact:KtmD
display=1
state=max
topmost=on

[rule]
name=E つながっていないディスプレイ
class=exact:KtmE
display=5
x=0
y=0

[rule]
name=F キャプションが変わったら
class=exact:KtmF
title=exact:kotemado-F-ready
display=1
x=300
y=300

[rule]
name=G 遅延
class=exact:KtmG
display=1
x=400
y=100
delay=600

[rule]
name=H DPI 非対応のアプリ
class=exact:KtmH
display=1
x=100
y=200
width=800
height=600

[rule]
name=I 最大化を解いて置く
class=exact:KtmI
display=1
state=normal
x=200
y=150
width=900
height=700

[rule]
name=J 最大化のまま元の位置だけ
class=exact:KtmJ
display=1
x=200
y=150
width=900
height=700

[rule]
name=K 定期的に
class=exact:KtmK
display=1
x=600
y=300
when=periodic
interval=100

[rule]
name=R v1 の when=show は定期的にとして読む
class=exact:KtmR
display=1
x=650
y=350
when=show
interval=100

[rule]
name=L 最初の 1 回だけ
class=exact:KtmL
display=1
x=600
y=300
when=once

[rule]
name=M プロセス名のワイルドカードだけ
exe=wildcard:ktm-m*.exe
x=700
y=400

[rule]
name=N 起動前から開いていた窓
class=exact:KtmN
display=1
x=50
y=60

[rule]
name=O 無効のルール
enabled=0
class=exact:KtmO
display=1
x=1000
y=500
"@
$iniPath = Join-Path $Work 'test.ini'
[IO.File]::WriteAllText($iniPath, $ini, (New-Object Text.UTF8Encoding($false)))

function Start-Target($name, [string[]]$extra, $exePath = $target) {
    $out = Join-Path $Work "$name.txt"
    $al = @('-out', $out, '-class', "Ktm$name") + $extra
    Start-Process -FilePath $exePath -ArgumentList $al -PassThru
}

& $exe -ini $iniPath -exit 2>$null
Start-Sleep -Milliseconds 300

$procs = @()
$procs += Start-Target 'N' @('-title', 'kotemado-N', '-life', '3500')
Start-Sleep -Milliseconds 500
$k = Start-Process -FilePath $exe -ArgumentList @('-ini', $iniPath, '-log') -PassThru
Start-Sleep -Milliseconds 800

$procs += Start-Target 'A' @('-notitle', '-life', '1200')
$procs += Start-Target 'B' @('-title', 'kotemado-B テスト', '-life', '1200')
$procs += Start-Target 'C' @('-title', 'kotemado-C', '-life', '1200')
$procs += Start-Target 'E' @('-title', 'kotemado-E', '-x', '77', '-y', '88', '-life', '1200')
$procs += Start-Target 'F' @('-title', 'kotemado-F-wait', '-retitle', '300', 'kotemado-F-ready', '-life', '1200')
$procs += Start-Target 'G' @('-title', 'kotemado-G', '-selfmove', '150', '5', '5', '-life', '1500')
$procs += Start-Target 'H' @('-title', 'kotemado-H', '-unaware', '-life', '1200')
$procs += Start-Target 'I' @('-title', 'kotemado-I', '-max', '-life', '1200')
$procs += Start-Target 'J' @('-title', 'kotemado-J', '-max', '-life', '1200')
$procs += Start-Target 'K' @('-title', 'kotemado-K', '-selfmove', '400', '5', '5', '-life', '3000')
$procs += Start-Target 'R' @('-title', 'kotemado-R', '-selfmove', '400', '5', '5', '-life', '1200')
$procs += Start-Target 'L' @('-title', 'kotemado-L', '-reshow', '400', '-life', '1200')
$procs += Start-Target 'O' @('-title', 'kotemado-O', '-x', '66', '-y', '77', '-life', '1200')
$procs += Start-Process -FilePath (Join-Path $Work 'ktm-m-target.exe') -PassThru `
              -ArgumentList @('-out', (Join-Path $Work 'M.txt'), '-class', 'KtmMwhatever', '-title', 'kotemado-M', '-life', '1200')
Start-Sleep -Milliseconds 300
$procs += Start-Target 'D' @('-title', 'kotemado-D', '-life', '1000')

$procs | ForEach-Object { $_.WaitForExit(10000) | Out-Null }
$cpu = (Get-Process -Id $k.Id).TotalProcessorTime.TotalMilliseconds
$ws  = (Get-Process -Id $k.Id).WorkingSet64
& $exe -ini $iniPath -exit
$k.WaitForExit(5000) | Out-Null

function Read-Result($name) {
    $p = Join-Path $Work "$name.txt"
    if (-not (Test-Path $p)) { return $null }
    $h = @{}
    foreach ($kv in (Get-Content $p -Raw).Trim().Split(' ')) {
        $a = $kv.Split('='); $h[$a[0]] = $a[1]
    }
    $h
}

$ml = $mon.Left; $mt = $mon.Top
$bw = [int]($wa.Width / 2); $bh = [int]($wa.Height / 2)
$bx = $wa.Left + [int][Math]::Truncate(($wa.Width - $bw) / 2)
$by = $wa.Top  + [int][Math]::Truncate(($wa.Height - $bh) / 2)
$cases = @(
    @('A', 'frame', "$($ml+100),$($mt+200),$($ml+900),$($mt+800)", 'キャプションなしの窓を絶対位置へ'),
    @('B', 'frame', "$bx,$by,$($bx+$bw),$($by+$bh)",                'キャプション前方一致・作業領域の中央・50%'),
    @('C', 'rect',  "$($ml+10),$($mt+20),$($ml+510),$($mt+420)",    '窓の矩形(透明な縁を含む)で合わせる'),
    @('D', 'zoomed', '1', '最大化'),
    @('D', 'topmost', '1', '最前面'),
    @('E', 'rect',  'start', 'つながっていないディスプレイなら動かさない'),
    @('F', 'frame', 'pos:300,300', 'キャプションが変わって条件に合ったら動かす'),
    @('G', 'frame', 'pos:400,100', '遅延: アプリが自分で動いた後に置き直す'),
    @('H', 'frame', "$($ml+100),$($mt+200),$($ml+900),$($mt+800)", 'DPI 非対応のアプリも物理ピクセルで置く'),
    @('I', 'frame', "$($ml+200),$($mt+150),$($ml+1100),$($mt+850)", '最大化を解いて置く'),
    @('J', 'zoomed', '1', '状態「変えない」なら最大化のまま'),
    @('J', 'normal', 'same:I.rect', '…元に戻したときの位置は I と同じ(最大化中の縁の見積もり)'),
    @('K', 'frame', 'pos:600,300', '「定期的に」はアプリが自分で動いても戻す'),
    @('K', 'wpc', 'max:12', '…位置が合っている間は触らない(3 秒・100ms 間隔で 30 回見回り)'),
    @('R', 'frame', 'pos:650,350', 'v1 の when=show は「定期的に」として読む'),
    @('L', 'rect',  'pos:0,0', '「最初の 1 回だけ」は再表示では動かさない'),
    @('M', 'frame', 'pos:700,400', 'プロセス名のワイルドカードだけで指定'),
    @('N', 'frame', 'pos:50,60', '起動前から開いていた窓にも適用'),
    @('O', 'rect',  'pos:66,77', '無効のルールは使わない')
)

$fail = 0
$rows = foreach ($c in $cases) {
    $r = Read-Result $c[0]
    $got = if ($r) { $r[$c[1]] } else { '(結果なし)' }
    $exp = $c[2]
    if ($exp -eq 'start') { $exp = '77,88,477,388' }
    if ($exp -like 'same:*') { $o = $exp.Substring(5).Split('.'); $exp = (Read-Result $o[0])[$o[1]] }
    $ok = $false
    if ($exp -like 'max:*') {
        $ok = $got -and [int]$got -le [int]$exp.Substring(4)
    } elseif ($exp -like 'pos:*') {
        $ok = $got -and ($got.Split(',')[0..1] -join ',') -eq $exp.Substring(4)
    } else { $ok = $got -eq $exp }
    if (-not $ok) { $fail++ }
    [pscustomobject]@{ 結果 = $(if ($ok) { 'OK' } else { 'NG' }); 項目 = $c[0]; 内容 = $c[3]; 期待 = $exp; 実測 = $got }
}
$rows | Format-Table -AutoSize | Out-String -Width 200
"画面: $($mon)  作業領域: $($wa)"
"kotemado の CPU 時間: $([int]$cpu) ms  ワーキングセット: $([int]($ws/1KB)) KB"
"---- log ----"
Get-Content "$Work\test.log" -Encoding UTF8
if ($fail) { "NG: $fail 件"; exit 1 } else { "すべて OK" }
