$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$images = New-Object 'Collections.Generic.List[byte[]]'
$sizes = @(16,24,32,48,64,128,256)
foreach ($size in $sizes) {
    $bitmap = New-Object Drawing.Bitmap($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.ScaleTransform(($size / 256.0),($size / 256.0))
    $blue = New-Object Drawing.SolidBrush([Drawing.ColorTranslator]::FromHtml('#1454d8'))
    $yellow = New-Object Drawing.SolidBrush([Drawing.ColorTranslator]::FromHtml('#ffd34b'))
    $white = New-Object Drawing.Pen([Drawing.Color]::White,22)
    $white.StartCap = $white.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $background = New-Object Drawing.Drawing2D.GraphicsPath
    foreach ($arc in @(@(16,16,104,104,180),@(136,16,104,104,270),@(136,136,104,104,0),@(16,136,104,104,90))) {
        $background.AddArc($arc[0],$arc[1],$arc[2],$arc[3],$arc[4],90)
    }
    $background.CloseFigure()
    $graphics.FillPath($blue,$background)
    $graphics.DrawLine($white,156,156,203,203)
    $graphics.FillEllipse($blue,56,48,112,112)
    $white.Width = 17
    $graphics.DrawEllipse($white,56,48,112,112)
    $white.Width = 8
    $graphics.DrawLine($white,84,81,139,81)
    $graphics.DrawLine($white,84,133,124,133)
    $graphics.FillRectangle($yellow,77,94,70,22)
    $textPen = New-Object Drawing.Pen($blue.Color,6)
    $textPen.StartCap = $textPen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLine($textPen,85,105,138,105)
    # DIB icon payloads also work in older GDI+ icon readers, unlike small PNG entries.
    $memory = New-Object IO.MemoryStream
    $dibWriter = New-Object IO.BinaryWriter($memory)
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $dibWriter.Write([uint32]40); $dibWriter.Write([int32]$size); $dibWriter.Write([int32]($size*2))
    $dibWriter.Write([uint16]1); $dibWriter.Write([uint16]32); $dibWriter.Write([uint32]0)
    $dibWriter.Write([uint32]($size*$size*4)); $dibWriter.Write([int32]0); $dibWriter.Write([int32]0)
    $dibWriter.Write([uint32]0); $dibWriter.Write([uint32]0)
    $locked = $bitmap.LockBits([Drawing.Rectangle]::new(0,0,$size,$size),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $row = New-Object byte[] ($size*4)
        for ($y=$size-1; $y -ge 0; $y--) {
            [Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($locked.Scan0,$y*$locked.Stride),$row,0,$row.Length)
            $dibWriter.Write($row)
        }
    } finally { $bitmap.UnlockBits($locked) }
    $dibWriter.Write((New-Object byte[] ($maskStride*$size)))
    $dibWriter.Flush(); $images.Add($memory.ToArray())
    $dibWriter.Dispose(); $memory.Dispose(); $textPen.Dispose(); $background.Dispose(); $white.Dispose()
    $yellow.Dispose(); $blue.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
$file = [IO.File]::Create((Join-Path $PSScriptRoot 'app.ico'))
$writer = New-Object IO.BinaryWriter($file)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i=0; $i -lt $sizes.Count; $i++) {
        $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
        $offset += $images[$i].Length
    }
    foreach ($bytes in $images) { $writer.Write($bytes) }
} finally { $writer.Dispose(); $file.Dispose() }
