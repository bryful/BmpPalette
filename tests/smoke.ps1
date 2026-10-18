param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('Win32', 'x64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$outputDir = "$Platform/$Configuration"
$exe = Join-Path $root "$outputDir/BmpPalette.exe"
$dir = Join-Path $PSScriptRoot 'generated'
New-Item -ItemType Directory -Force $dir | Out-Null
function Assert($condition, $message) { if (!$condition) { throw $message } }
function Make-Bmp($path, $bits, $core = $false) {
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    $count = if ($bits -eq 8) { 3 } else { 0 }
    $header = if ($core) { 12 } else { 40 }
    if ($core) { $count = 2; $bits = 1 }
    $entry = if ($core) { 3 } else { 4 }
    $offset = 14 + $header + $count * $entry
    $writer.Write([byte[]](66,77)); $writer.Write([uint32]($offset+4))
    $writer.Write([uint32]0); $writer.Write([uint32]$offset); $writer.Write([uint32]$header)
    if ($core) { $writer.Write([uint16]1); $writer.Write([uint16]1) }
    else { $writer.Write([int]1); $writer.Write([int]-1) }
    $writer.Write([uint16]1); $writer.Write([uint16]$bits)
    if (!$core) {
        $writer.Write([uint32]0); $writer.Write([uint32]4)
        $writer.Write([int]0); $writer.Write([int]0); $writer.Write([uint32]$count); $writer.Write([uint32]0)
    }
    $colors = @(@(0,0,0), @(3,4,28), @(4,15,8))
    for ($i=0; $i -lt $count; $i++) {
        $writer.Write([byte[]]$colors[$i]); if (!$core) { $writer.Write([byte]0) }
    }
    $writer.Write([uint32]0)
    [IO.File]::WriteAllBytes($path,$stream.ToArray()); $writer.Dispose(); $stream.Dispose()
}

function Read-Colors($path) {
    $text = Get-Content -Raw -LiteralPath $path
    $triplets = $text | ConvertFrom-Json
    foreach ($rgb in $triplets) {
        Assert ($rgb.Count -eq 3) 'Expected RGB triplet'
        ([int]$rgb[0] -shl 16) -bor ([int]$rgb[1] -shl 8) -bor [int]$rgb[2]
    }
}
function Run-Ok([string[]]$Arguments) {
    $result = & $exe @Arguments
    Assert ($LASTEXITCODE -eq 0) ('Command failed: ' + ($Arguments -join ' '))
    return $result
}
function Run-Fail([string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $exe @Arguments 2>$null | Out-Null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    Assert ($code -eq 1) ('Expected failure: ' + ($Arguments -join ' '))
}
$bmp = Join-Path $dir '日本語 palette.bmp'
Make-Bmp $bmp 8
$base = (Join-Path ([IO.Path]::GetDirectoryName($bmp)) ([IO.Path]::GetFileNameWithoutExtension($bmp)))
$pal = Run-Ok @('-pal', $base)
Assert (($pal -join ',') -eq '#000000,#1c0403,#080f04') 'RGB order incorrect'
$info = Run-Ok @('-info',$base)
Assert (($info -join "`n") -match 'Palette colors: 3') 'info color count incorrect'
$txt = Join-Path $dir 'palette.txt'
Run-Ok @('-pal',$bmp,'-o',$txt) | Out-Null
Assert (([IO.File]::ReadAllText($txt)) -eq (($pal -join "`n")+"`n")) 'Text output differs from stdout'
$act = Join-Path $dir 'palette.act'
Run-Ok @('-pal',$bmp,'-o',$act) | Out-Null
$data = [IO.File]::ReadAllBytes($act)
Assert ($data.Length -eq 772 -and $data[3] -eq 28 -and $data[4] -eq 4 -and $data[5] -eq 3) 'ACT RGB invalid'
Assert ($data[768] -eq 0 -and $data[769] -eq 3 -and $data[770] -eq 255 -and $data[771] -eq 255) 'ACT footer invalid'
$json = Join-Path $dir 'palette.json'
Run-Ok @('-pal',$bmp,'-o',$json) | Out-Null
$values = (Read-Colors $json)
Assert ((Get-Content -Raw $json).Trim() -eq '[[0,0,0],[28,4,3],[8,15,4]]') 'RGB array output incorrect'
$cOutput = Join-Path $dir 'palette.c'
Run-Ok @('-pal',$bmp,'-o',$cOutput) | Out-Null
Assert ((Get-Content -Raw $cOutput).Trim() -eq '{0x000000,0x1C0403,0x080F04}') 'C output incorrect'
Assert ($values.Count -eq 3 -and $values[1] -eq 0x1c0403) 'JSON RGB invalid'
$extensionless = Join-Path $dir 'palette-default'
Run-Ok @('-pal',$bmp,'-o',$extensionless) | Out-Null
Assert ((Get-Content -Raw ($extensionless+'.gpl')).StartsWith('GIMP Palette')) 'Default GPL extension failed'
$core = Join-Path $dir 'core.bmp'
Make-Bmp $core 1 $true
$pal = Run-Ok @('-pal',$core)
Assert (($pal -join ',') -eq '#000000,#1c0403') 'CORE palette invalid'

# Odd width and multiple rows exercise nibble packing, padding and row orientation.
function Make-Rgb($path, $topDown) {
    $stream = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($stream)
    $w.Write([byte[]](66,77)); $w.Write([uint32]102); $w.Write([uint32]0); $w.Write([uint32]54)
    $w.Write([uint32]40); $w.Write([int]5)
    $height = if ($topDown) { -3 } else { 3 }
    $w.Write([int]$height); $w.Write([uint16]1); $w.Write([uint16]24)
    $w.Write([uint32]0); $w.Write([uint32]48); $w.Write([uint32]0); $w.Write([uint32]0); $w.Write([uint32]0); $w.Write([uint32]0)
    $colors = @(0, 1, 0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0xff00ff, 0xffff00, 0x00ffff, 0x808080, 0x800000, 0x008000, 0x000080, 0x123456, 0x654321)
    for ($y=0; $y -lt 3; $y++) {
        for ($x=0; $x -lt 5; $x++) {
            $v = $colors[$y*5+$x]
            $w.Write([byte]($v-band 255)); $w.Write([byte](($v-shr 8)-band 255)); $w.Write([byte](($v-shr 16)-band 255))
        }
        $w.Write([byte]0)
    }
    [IO.File]::WriteAllBytes($path,$stream.ToArray()); $w.Dispose(); $stream.Dispose()
    return $colors
}
$rgb = Join-Path $dir 'rgb-a.bmp'
$expected = @(Make-Rgb $rgb $true)
$rgb2 = Join-Path $dir 'rgb-b.bmp'
Make-Rgb $rgb2 $false | Out-Null
$generatedJson = Join-Path $dir 'generated.json'
Run-Ok @('-makepal',((Join-Path ([IO.Path]::GetDirectoryName($rgb)) ([IO.Path]::GetFileNameWithoutExtension($rgb)))),'-o',$generatedJson) | Out-Null
$colors = (Read-Colors $generatedJson)
Assert ($colors.Count -eq 16 -and $colors[0] -eq 0xff00ff) 'Generated palette index 0 invalid'
foreach ($v in $expected) { Assert ($colors[1..15] -contains $v) 'A <=15-color image should preserve every source color' }
$generatedGpl = Join-Path $dir 'generated.gpl'
Run-Ok @('-makepal',$rgb,'-o',$generatedGpl) | Out-Null
Run-Ok @('-makepal',$rgb2) | Out-Null
Assert (Test-Path ([IO.Path]::ChangeExtension($rgb2,'.gpl'))) 'Default makepal output failed'
$reduceDir = Join-Path $dir ('reduce-'+[guid]::NewGuid().ToString('N'))
Run-Ok @('-redu',(Join-Path $dir 'rgb-*'),'-p',$generatedJson,'-o',$reduceDir) | Out-Null
function Check-Reduced($path, $height) {
    $b = [IO.File]::ReadAllBytes($path)
    Assert ([BitConverter]::ToUInt16($b,28) -eq 4) 'Reduced bit depth incorrect'
    Assert ([BitConverter]::ToInt32($b,22) -eq $height) 'Height/orientation changed'
    Assert ([BitConverter]::ToUInt32($b,10) -eq 118 -and $b.Length -eq 130) 'Pixel layout invalid'
    Assert ($b[54] -eq 255 -and $b[55] -eq 0 -and $b[56] -eq 255) 'Reserved magenta not saved'
    for ($y=0;$y -lt 3;$y++) {
        for ($x=0;$x -lt 5;$x++) {
            $byte = $b[118+$y*4+[int][Math]::Floor($x/2)]
            $index = if ($x%2 -eq 0) { $byte-shr 4 } else { $byte-band 15 }
            Assert ($index -gt 0 -and $index -lt 16) 'Reserved index 0 used for a pixel'
            Assert ($colors[$index] -eq $expected[$y*5+$x]) 'Pixel color/row changed'
        }
        Assert (($b[120+$y*4]-band 15) -eq 0 -and $b[121+$y*4] -eq 0) 'Padding not zero'
    }
}
Check-Reduced (Join-Path $reduceDir 'rgb-a.bmp') -3
Check-Reduced (Join-Path $reduceDir 'rgb-b.bmp') 3
$before = [IO.File]::ReadAllBytes((Join-Path $reduceDir 'rgb-a.bmp'))
Run-Ok @('-redu',$rgb,'-p',$generatedGpl,'-o',$reduceDir) | Out-Null
Assert (Test-Path (Join-Path $reduceDir 'rgb-a_1.bmp')) 'Collision suffix missing'
Assert ([Convert]::ToBase64String($before) -eq [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $reduceDir 'rgb-a.bmp')))) 'Existing output overwritten'
Check-Reduced (Join-Path $reduceDir 'rgb-a_1.bmp') -3
# Accept the requested hexadecimal input notation, too.
$hexPalette = Join-Path $dir 'hex.json'
('['+(($colors | ForEach-Object { '0x{0:X6}' -f [int]$_ }) -join ',')+']') | Set-Content $hexPalette
Run-Ok @('-redu',$rgb,'-p',$hexPalette,'-o',$reduceDir) | Out-Null
Check-Reduced (Join-Path $reduceDir 'rgb-a_2.bmp') -3
Push-Location $reduceDir
try { Run-Ok @('-redu',$rgb,'-p',$generatedJson) | Out-Null } finally { Pop-Location }
Assert (Test-Path (Join-Path $reduceDir 'rgb-a_3.bmp')) 'Default output directory incorrect'
Run-Fail @('-pal',$rgb)
Run-Fail @('-makepal',$bmp,'-o',$generatedJson)
Run-Fail @('-pal',$bmp,'-o',$bmp)
Run-Fail @('-pal',$bmp,'-o',(Join-Path $dir 'bad.xyz'))
Run-Fail @('-redu',$rgb,'-p',$json,'-o',$reduceDir)
Run-Fail @('-redu',(Join-Path $dir 'nonexistent*.bmp'),'-p',$generatedJson)
Run-Fail @('-info',$bmp,'-o',$txt)
Run-Fail @('-pal',$bmp,'-o')
$bad = Join-Path $dir 'bad.bmp'
[IO.File]::WriteAllBytes($bad,[byte[]](66,77))
Run-Fail @('-info',$bad)
$truncated = Join-Path $dir 'truncated.bmp'
$b = [IO.File]::ReadAllBytes($rgb)
[IO.File]::WriteAllBytes($truncated,$b[0..60])
Run-Fail @('-makepal',$truncated)
# Index 0 must never win even when it exactly matches a source pixel.
$blackPalette = Join-Path $dir 'black.json'
('[16711935,'+((@(0)*15) -join ',')+']') | Set-Content $blackPalette
Run-Ok @('-redu',$rgb,'-p',$blackPalette,'-o',$reduceDir) | Out-Null
$black = [IO.File]::ReadAllBytes((Join-Path $reduceDir 'rgb-a_4.bmp'))
for ($y=0;$y -lt 3;$y++) {
    Assert ($black[118+$y*4] -eq 0x11 -and $black[119+$y*4] -eq 0x11 -and $black[120+$y*4] -eq 0x10) 'Index 0 was selected or decimal palette was not read'
}
# A continuous gradient requires quantization rather than only copying <=15 colors.
$gradient = Join-Path $dir 'gradient.bmp'
$stream = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($stream)
$w.Write([byte[]](66,77)); $w.Write([uint32]822); $w.Write([uint32]0); $w.Write([uint32]54)
$w.Write([uint32]40); $w.Write([int]256); $w.Write([int]1); $w.Write([uint16]1); $w.Write([uint16]24)
$w.Write([uint32]0); $w.Write([uint32]768); $w.Write([uint32]0); $w.Write([uint32]0); $w.Write([uint32]0); $w.Write([uint32]0)
for ($i=0;$i -lt 256;$i++) { $w.Write([byte]$i); $w.Write([byte]$i); $w.Write([byte]$i) }
[IO.File]::WriteAllBytes($gradient,$stream.ToArray()); $w.Dispose(); $stream.Dispose()
$gradientPalette = Join-Path $dir 'gradient.json'
Run-Ok @('-makepal',$gradient,'-o',$gradientPalette) | Out-Null
$gray = Read-Colors $gradientPalette
Assert ($gray.Count -eq 16) 'Gradient palette size incorrect'
Run-Ok @('-redu',$gradient,'-p',$gradientPalette,'-o',$reduceDir) | Out-Null
$reduced = [IO.File]::ReadAllBytes((Join-Path $reduceDir 'gradient.bmp'))
$errorSum = 0.0
for ($x=0;$x -lt 256;$x++) {
    $packed = $reduced[118+[int][Math]::Floor($x/2)]
    $index = if ($x%2 -eq 0) { $packed-shr 4 } else { $packed-band 15 }
    Assert ($index -ge 1) 'Gradient uses reserved index'
    $v = $gray[$index]-band 255
    $errorSum += ($v-$x)*($v-$x)
}
Assert (($errorSum/256) -lt 100) 'Gradient quantization error too large'
$invalidJson = Join-Path $dir 'invalid-rgb.json'
foreach ($invalid in @('[[256,0,0]]','[[1,2]]','[[1,2,3,4]]','[[1.5,0,0]]','[[-1,0,0]]','[[01,0,0]]')) {
    $invalid | Set-Content $invalidJson
    Run-Fail @('-redu',$rgb,'-p',$invalidJson,'-o',$reduceDir)
}
Run-Ok @('-help') | Out-Null
Write-Output 'All smoke tests passed.'





