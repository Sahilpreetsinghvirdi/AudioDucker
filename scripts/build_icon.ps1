# Generates src/resources/app.ico (classic 32bpp BMP frames) with a speaker glyph.
# Run from the project root:  powershell -ExecutionPolicy Bypass -File scripts/build_icon.ps1
param(
    [string]$OutFile = "$PSScriptRoot\..\src\resources\app.ico"
)

Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64)

function Draw-SpeakerIcon([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $u = $size / 32.0
    $green = [System.Drawing.Color]::FromArgb(255, 46, 140, 66)
    $dark  = [System.Drawing.Color]::FromArgb(255, 82, 109, 62)

    $box = New-Object System.Drawing.SolidBrush($dark)
    $g.FillRectangle($box, [float](3 * $u), [float](12 * $u), [float](9 * $u), [float](8 * $u))

    $cone = New-Object System.Drawing.SolidBrush($green)
    $pts = @(
        (New-Object System.Drawing.PointF([float](12 * $u), [float](12 * $u))),
        (New-Object System.Drawing.PointF([float](20 * $u), [float](7 * $u))),
        (New-Object System.Drawing.PointF([float](20 * $u), [float](25 * $u)))
    )
    $g.FillPolygon($cone, $pts)

    $pen = New-Object System.Drawing.Pen($green, [float](2.2 * $u))
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawArc($pen, [float](20 * $u), [float](9 * $u), [float](7 * $u), [float](14 * $u), -55, 110)
    $g.DrawArc($pen, [float](23 * $u), [float](5 * $u), [float](9 * $u), [float](22 * $u), -55, 110)

    $g.Dispose()
    return $bmp
}

# Writes one classic 32bpp DIB frame (BITMAPINFOHEADER + BGRA xor data + and mask)
# into $dataWriter and its ICONDIRENTRY into $entryWriter. Returns the byte count.
function Write-IconFrame([System.Drawing.Bitmap]$bmp, $entryWriter, $dataWriter, [ref]$offset) {
    $size = $bmp.Width
    $xorLen = $size * $size * 4
    $andStride = ([int][Math]::Ceiling($size / 32.0)) * 4
    $andLen = $andStride * $size
    $bytesInRes = 40 + $xorLen + $andLen

    # ICONDIRENTRY
    $entryWriter.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $entryWriter.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $entryWriter.Write([Byte]0)         # colors
    $entryWriter.Write([Byte]0)         # reserved
    $entryWriter.Write([UInt16]1)       # planes
    $entryWriter.Write([UInt16]32)      # bpp
    $entryWriter.Write([UInt32]$bytesInRes)
    $entryWriter.Write([UInt32]$offset.Value)

    # BITMAPINFOHEADER (40 bytes)
    $dataWriter.Write([UInt32]40)
    $dataWriter.Write([Int32]$size)
    $dataWriter.Write([Int32]($size * 2))   # combined xor+and height
    $dataWriter.Write([UInt16]1)
    $dataWriter.Write([UInt16]32)
    $dataWriter.Write([UInt32]0)
    $dataWriter.Write([UInt32]($xorLen + $andLen))
    $dataWriter.Write([Int32]0)
    $dataWriter.Write([Int32]0)
    $dataWriter.Write([UInt32]0)
    $dataWriter.Write([UInt32]0)

    # XOR data: BGRA, bottom-up
    for ($y = $size - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $size; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $dataWriter.Write([Byte]$c.B)
            $dataWriter.Write([Byte]$c.G)
            $dataWriter.Write([Byte]$c.R)
            $dataWriter.Write([Byte]$c.A)
        }
    }
    # AND mask: all opaque
    $zero = New-Object byte[] $andLen
    $dataWriter.Write($zero)

    $offset.Value += $bytesInRes
}

# Build the multi-frame ICO in one pass: header, entries, then concatenated DIBs.
$header = New-Object System.IO.MemoryStream
$hbw = New-Object System.IO.BinaryWriter($header)

$entryMs = New-Object System.IO.MemoryStream
$entryBw = New-Object System.IO.BinaryWriter($entryMs)
$dataMs = New-Object System.IO.MemoryStream
$dataBw = New-Object System.IO.BinaryWriter($dataMs)

$offset = 6 + 16 * $sizes.Count

$hbw.Write([UInt16]0)       # reserved
$hbw.Write([UInt16]1)       # type: icon
$hbw.Write([UInt16]$sizes.Count)

foreach ($size in $sizes) {
    $bmp = Draw-SpeakerIcon $size
    Write-IconFrame -Bmp $bmp -EntryWriter $entryBw -DataWriter $dataBw -Offset ([ref]$offset)
    $bmp.Dispose()
}

$entryBw.Flush()
$dataBw.Flush()
$hbw.Write($entryMs.ToArray())
$hbw.Write($dataMs.ToArray())
$hbw.Flush()

$ico = $header.ToArray()
$hbw.Dispose()
$entryBw.Dispose()
$dataBw.Dispose()

$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
[System.IO.File]::WriteAllBytes($OutFile, $ico)
Write-Output "Wrote $OutFile ($($ico.Length) bytes)"
