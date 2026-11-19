# Draws the Perch logo (same shapes as perch.svg) at every icon size and writes perch.ico
# (PNG entries) plus perch-256.png. Run it after changing the logo:
#   powershell -ExecutionPolicy Bypass -File assets\make_icon.ps1
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$sizes = 16, 20, 24, 32, 40, 48, 64, 256

function Draw-Logo([int]$px) {
    $bmp = New-Object System.Drawing.Bitmap $px, $px, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $px / 64.0
    $g.ScaleTransform($k, $k)

    # tile: 64 x 64, corner radius 14
    $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 28
    $tile.AddArc(0, 0, $d, $d, 180, 90); $tile.AddArc(64 - $d, 0, $d, $d, 270, 90)
    $tile.AddArc(64 - $d, 64 - $d, $d, $d, 0, 90); $tile.AddArc(0, 64 - $d, $d, $d, 90, 90); $tile.CloseFigure()
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0x0B, 0x0B, 0x0C))), $tile)

    # P: M23 48 V17 H35.5 a9.5 9.5 0 0 1 0 19 H23, stroke 7, round caps and joins
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddLine(23, 48, 23, 17); $p.AddLine(23, 17, 35.5, 17)
    $p.AddArc(26, 17, 19, 19, -90, 180)
    $p.AddLine(35.5, 36, 23, 36)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(0xF4, 0xF4, 0xF2)), 7
    $pen.StartCap = $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $p)

    # mic dot
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0xFF, 0x9F, 0x0A))), 41, 42, 8, 8)
    $g.Dispose()
    return $bmp
}

# Classic layout: 32-bit bitmaps (DIB) for the small sizes, PNG for 256 px.
function To-Dib($bmp) {
    $s = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $ms
    $maskRow = [int]([Math]::Ceiling($s / 32.0) * 4)
    $w.Write([uint32]40); $w.Write([int32]$s); $w.Write([int32]($s * 2)) # height covers colour + mask
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]0)
    $w.Write([uint32]($s * $s * 4 + $maskRow * $s)); $w.Write([int32]0); $w.Write([int32]0); $w.Write([uint32]0); $w.Write([uint32]0)
    for ($y = $s - 1; $y -ge 0; $y--) { # bottom-up BGRA
        for ($x = 0; $x -lt $s; $x++) { $c = $bmp.GetPixel($x, $y); $w.Write([byte]$c.B); $w.Write([byte]$c.G); $w.Write([byte]$c.R); $w.Write([byte]$c.A) }
    }
    $w.Write((New-Object byte[] ($maskRow * $s))) # AND mask: all zero (alpha does the work)
    $w.Flush()
    return , $ms.ToArray()
}

$pngs = foreach ($s in $sizes) {
    $bmp = Draw-Logo $s
    if ($s -ge 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Save((Join-Path $PSScriptRoot 'perch-256.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        $data = $ms.ToArray()
    } else {
        $data = To-Dib $bmp
    }
    $bmp.Dispose()
    , $data
}

# ICO: header, one 16-byte entry per size, then the PNG data.
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s }))); $w.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $w.Write([byte]0); $w.Write([byte]0); $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($png in $pngs) { $w.Write($png) }
$w.Flush()
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'perch.ico'), $out.ToArray())
"perch.ico: $($sizes -join ', ') px, $($out.Length) bytes"
