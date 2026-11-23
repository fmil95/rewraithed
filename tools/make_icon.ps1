# Builds art/rewraithed.ico (the icon of rewraithed.exe and
# rewraithed-launcher.exe, port/res/rewraithed.rc) from a square PNG:
# 256, 64, 48, 32, 24 and 16-pixel images, each stored as PNG.
#   powershell -File tools/make_icon.ps1 [-Source art/shiftglyph.png]
param(
    [string]$Source = "$PSScriptRoot/../art/shiftglyph.png",
    [string]$Output = "$PSScriptRoot/../art/rewraithed.ico"
)

Add-Type -AssemblyName System.Drawing

$sizes = 256, 64, 48, 32, 24, 16
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$images = @()

foreach ($n in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.DrawImage($src, 0, 0, $n, $n)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $images += , $ms.ToArray()
}

$src.Dispose()

$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)

$offset = 6 + 16 * $sizes.Count

for ($i = 0; $i -lt $sizes.Count; $i++) {
    $n = $sizes[$i] % 256        # 0 means 256
    $w.Write([byte]$n); $w.Write([byte]$n); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$i].Length); $w.Write([uint32]$offset)
    $offset += $images[$i].Length
}

foreach ($img in $images) { $w.Write($img) }

$w.Flush()
[System.IO.File]::WriteAllBytes([System.IO.Path]::GetFullPath($Output), $out.ToArray())
