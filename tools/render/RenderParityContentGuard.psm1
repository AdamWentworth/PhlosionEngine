$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

if (-not ("Phlosion.Tools.RenderParity.ContentGuard" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace Phlosion.Tools.RenderParity
{
    public sealed class ContentGuardResult
    {
        public string Name { get; set; }
        public int ImageWidth { get; set; }
        public int ImageHeight { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public int Width { get; set; }
        public int Height { get; set; }
        public long PixelCount { get; set; }
        public double MeanLuminance { get; set; }
        public double LuminanceStandardDeviation { get; set; }
        public double NearBlackPixelRatio { get; set; }
        public double MidtonePixelRatio { get; set; }
        public double RedPixelRatio { get; set; }
        public double OrangePixelRatio { get; set; }
        public double BrightNeutralPixelRatio { get; set; }
        public int NearBlackLuminanceMaximum { get; set; }
        public int MidtoneLuminanceMinimum { get; set; }
        public int MidtoneLuminanceMaximum { get; set; }
        public double MaximumNearBlackPixelRatio { get; set; }
        public double MinimumMidtonePixelRatio { get; set; }
        public bool Passed { get; set; }
        public string[] FailureReasons { get; set; }
    }

    public static class ContentGuard
    {
        private static byte[] ReadPixels(Bitmap bitmap)
        {
            Rectangle rect = new Rectangle(0, 0, bitmap.Width, bitmap.Height);
            BitmapData data = bitmap.LockBits(
                rect,
                ImageLockMode.ReadOnly,
                PixelFormat.Format32bppArgb);
            try
            {
                int stride = Math.Abs(data.Stride);
                byte[] source = new byte[stride * bitmap.Height];
                byte[] packed = new byte[bitmap.Width * bitmap.Height * 4];
                Marshal.Copy(data.Scan0, source, 0, source.Length);
                int rowBytes = bitmap.Width * 4;
                for (int y = 0; y < bitmap.Height; ++y)
                {
                    int sourceRow = data.Stride >= 0 ? y : bitmap.Height - 1 - y;
                    Buffer.BlockCopy(source, sourceRow * stride, packed, y * rowBytes, rowBytes);
                }
                return packed;
            }
            finally
            {
                bitmap.UnlockBits(data);
            }
        }

        private static Bitmap LoadNormalized(string path)
        {
            using (Bitmap source = new Bitmap(path))
            {
                Rectangle rect = new Rectangle(0, 0, source.Width, source.Height);
                return source.Clone(rect, PixelFormat.Format32bppArgb);
            }
        }

        private static int Clamp(int value, int minimum, int maximum)
        {
            return Math.Min(maximum, Math.Max(minimum, value));
        }

        private static void ValidateUnitInterval(string name, double value)
        {
            if (Double.IsNaN(value) || Double.IsInfinity(value) ||
                value < 0.0 || value > 1.0)
            {
                throw new ArgumentOutOfRangeException(
                    name,
                    value,
                    "Expected a finite value between 0 and 1.");
            }
        }

        public static ContentGuardResult Analyze(
            string imagePath,
            string name,
            double normalizedX,
            double normalizedY,
            double normalizedWidth,
            double normalizedHeight,
            int nearBlackLuminanceMaximum,
            int midtoneLuminanceMinimum,
            int midtoneLuminanceMaximum,
            double maximumNearBlackPixelRatio,
            double minimumMidtonePixelRatio,
            double minimumRedPixelRatio,
            double minimumBrightNeutralPixelRatio,
            double minimumOrangePixelRatio)
        {
            ValidateUnitInterval("normalizedX", normalizedX);
            ValidateUnitInterval("normalizedY", normalizedY);
            ValidateUnitInterval("normalizedWidth", normalizedWidth);
            ValidateUnitInterval("normalizedHeight", normalizedHeight);
            ValidateUnitInterval(
                "maximumNearBlackPixelRatio",
                maximumNearBlackPixelRatio);
            ValidateUnitInterval(
                "minimumMidtonePixelRatio",
                minimumMidtonePixelRatio);
            ValidateUnitInterval("minimumRedPixelRatio", minimumRedPixelRatio);
            ValidateUnitInterval("minimumOrangePixelRatio", minimumOrangePixelRatio);
            ValidateUnitInterval("minimumBrightNeutralPixelRatio", minimumBrightNeutralPixelRatio);

            if (String.IsNullOrWhiteSpace(name))
            {
                throw new ArgumentException("Content guard name cannot be empty.", "name");
            }
            if (normalizedWidth <= 0.0 || normalizedHeight <= 0.0 ||
                normalizedX + normalizedWidth > 1.0 + 1e-9 ||
                normalizedY + normalizedHeight > 1.0 + 1e-9)
            {
                throw new ArgumentOutOfRangeException(
                    "normalizedWidth",
                    "Content guard rectangle must have positive dimensions and fit inside the image.");
            }
            if (nearBlackLuminanceMaximum < 0 ||
                nearBlackLuminanceMaximum > 255 ||
                midtoneLuminanceMinimum < 0 ||
                midtoneLuminanceMinimum > 255 ||
                midtoneLuminanceMaximum < 0 ||
                midtoneLuminanceMaximum > 255 ||
                nearBlackLuminanceMaximum >= midtoneLuminanceMinimum ||
                midtoneLuminanceMinimum >= midtoneLuminanceMaximum)
            {
                throw new ArgumentOutOfRangeException(
                    "midtoneLuminanceMinimum",
                    "Luminance thresholds must be ordered within the byte range.");
            }

            using (Bitmap bitmap = LoadNormalized(imagePath))
            {
                int left = Clamp(
                    (int)Math.Floor(normalizedX * bitmap.Width),
                    0,
                    bitmap.Width - 1);
                int top = Clamp(
                    (int)Math.Floor(normalizedY * bitmap.Height),
                    0,
                    bitmap.Height - 1);
                int right = Clamp(
                    (int)Math.Ceiling((normalizedX + normalizedWidth) * bitmap.Width),
                    left + 1,
                    bitmap.Width);
                int bottom = Clamp(
                    (int)Math.Ceiling((normalizedY + normalizedHeight) * bitmap.Height),
                    top + 1,
                    bitmap.Height);

                byte[] pixels = ReadPixels(bitmap);
                long pixelCount = (long)(right - left) * (bottom - top);
                long nearBlackPixels = 0;
                long midtonePixels = 0;
                long redPixels = 0;
                long orangePixels = 0;
                long brightNeutralPixels = 0;
                double luminanceSum = 0.0;
                double luminanceSquaredSum = 0.0;

                for (int y = top; y < bottom; ++y)
                {
                    for (int x = left; x < right; ++x)
                    {
                        int pixel = (y * bitmap.Width + x) * 4;
                        int r = pixels[pixel + 2], g = pixels[pixel + 1], b = pixels[pixel];
                        if (r >= 150 && g <= 100 && b <= 100) ++redPixels;
                        if (r >= 200 && g >= 100 && g <= 200 && b <= 120) ++orangePixels;
                        if (Math.Min(r, Math.Min(g, b)) >= 180 &&
                            Math.Max(r, Math.Max(g, b)) - Math.Min(r, Math.Min(g, b)) <= 40)
                            ++brightNeutralPixels;
                        double luminance =
                            0.2126 * pixels[pixel + 2] +
                            0.7152 * pixels[pixel + 1] +
                            0.0722 * pixels[pixel + 0];
                        luminanceSum += luminance;
                        luminanceSquaredSum += luminance * luminance;
                        if (luminance <= nearBlackLuminanceMaximum) ++nearBlackPixels;
                        if (luminance >= midtoneLuminanceMinimum &&
                            luminance <= midtoneLuminanceMaximum)
                        {
                            ++midtonePixels;
                        }
                    }
                }

                double safePixelCount = Math.Max(1.0, pixelCount);
                double mean = luminanceSum / safePixelCount;
                double variance = Math.Max(
                    0.0,
                    luminanceSquaredSum / safePixelCount - mean * mean);
                double nearBlackRatio = nearBlackPixels / safePixelCount;
                double midtoneRatio = midtonePixels / safePixelCount;
                List<string> failures = new List<string>();
                double redRatio = redPixels / safePixelCount;
                double orangeRatio = orangePixels / safePixelCount;
                if (orangeRatio < minimumOrangePixelRatio)
                    failures.Add(String.Format("orange ratio {0:F6} is below minimum {1:F6}", orangeRatio, minimumOrangePixelRatio));
                double brightNeutralRatio = brightNeutralPixels / safePixelCount;
                if (redRatio < minimumRedPixelRatio)
                    failures.Add(String.Format("red ratio {0:F6} is below minimum {1:F6}", redRatio, minimumRedPixelRatio));
                if (brightNeutralRatio < minimumBrightNeutralPixelRatio)
                    failures.Add(String.Format("bright-neutral ratio {0:F6} is below minimum {1:F6}", brightNeutralRatio, minimumBrightNeutralPixelRatio));
                if (nearBlackRatio > maximumNearBlackPixelRatio)
                {
                    failures.Add(String.Format(
                        "near-black ratio {0:F6} exceeds maximum {1:F6}",
                        nearBlackRatio,
                        maximumNearBlackPixelRatio));
                }
                if (midtoneRatio < minimumMidtonePixelRatio)
                {
                    failures.Add(String.Format(
                        "midtone ratio {0:F6} is below minimum {1:F6}",
                        midtoneRatio,
                        minimumMidtonePixelRatio));
                }

                return new ContentGuardResult
                {
                    Name = name,
                    ImageWidth = bitmap.Width,
                    ImageHeight = bitmap.Height,
                    X = left,
                    Y = top,
                    Width = right - left,
                    Height = bottom - top,
                    PixelCount = pixelCount,
                    MeanLuminance = mean,
                    LuminanceStandardDeviation = Math.Sqrt(variance),
                    NearBlackPixelRatio = nearBlackRatio,
                    MidtonePixelRatio = midtoneRatio,
                    RedPixelRatio = redRatio,
                    OrangePixelRatio = orangeRatio,
                    BrightNeutralPixelRatio = brightNeutralRatio,
                    NearBlackLuminanceMaximum = nearBlackLuminanceMaximum,
                    MidtoneLuminanceMinimum = midtoneLuminanceMinimum,
                    MidtoneLuminanceMaximum = midtoneLuminanceMaximum,
                    MaximumNearBlackPixelRatio = maximumNearBlackPixelRatio,
                    MinimumMidtonePixelRatio = minimumMidtonePixelRatio,
                    Passed = failures.Count == 0,
                    FailureReasons = failures.ToArray()
                };
            }
        }
    }
}
'@ -ReferencedAssemblies System.Drawing
}

function Get-RenderParityGuardValue {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Guard,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [object]$DefaultValue
    )

    $property = $Guard.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return $DefaultValue
    }
    return $property.Value
}

function Test-RenderParityImageContent {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ImagePath,
        [Parameter(Mandatory = $true)]
        [object]$Guard
    )

    $minimumVariation = [double](Get-RenderParityGuardValue `
        -Guard $Guard -Name "minimumLuminanceStandardDeviation" -DefaultValue 0.0)
    if ([double]::IsNaN($minimumVariation) -or [double]::IsInfinity($minimumVariation) -or
        $minimumVariation -lt 0.0 -or $minimumVariation -gt 255.0) {
        throw 'minimumLuminanceStandardDeviation must be finite and between 0 and 255.'
    }
    $result = [Phlosion.Tools.RenderParity.ContentGuard]::Analyze(
        $ImagePath,
        [string](Get-RenderParityGuardValue -Guard $Guard -Name "name" -DefaultValue ""),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "x" -DefaultValue 0.0),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "y" -DefaultValue 0.0),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "width" -DefaultValue 0.0),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "height" -DefaultValue 0.0),
        [int](Get-RenderParityGuardValue `
            -Guard $Guard `
            -Name "nearBlackLuminanceMaximum" `
            -DefaultValue 16),
        [int](Get-RenderParityGuardValue `
            -Guard $Guard `
            -Name "midtoneLuminanceMinimum" `
            -DefaultValue 64),
        [int](Get-RenderParityGuardValue `
            -Guard $Guard `
            -Name "midtoneLuminanceMaximum" `
            -DefaultValue 190),
        [double](Get-RenderParityGuardValue `
            -Guard $Guard `
            -Name "maximumNearBlackPixelRatio" `
            -DefaultValue 0.1),
        [double](Get-RenderParityGuardValue `
            -Guard $Guard `
            -Name "minimumMidtonePixelRatio" `
            -DefaultValue 0.1),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "minimumRedPixelRatio" -DefaultValue 0.0),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "minimumBrightNeutralPixelRatio" -DefaultValue 0.0),
        [double](Get-RenderParityGuardValue -Guard $Guard -Name "minimumOrangePixelRatio" -DefaultValue 0.0))
    if ($result.LuminanceStandardDeviation -lt $minimumVariation) {
        $result.Passed = $false
        $result.FailureReasons = @($result.FailureReasons) + @(
            "luminance variation $($result.LuminanceStandardDeviation) is below minimum $minimumVariation")
    }
    return $result
}

Export-ModuleMember -Function Test-RenderParityImageContent
