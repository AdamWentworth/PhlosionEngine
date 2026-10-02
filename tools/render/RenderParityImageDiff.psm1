$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

if (-not ("Phlosion.Tools.RenderParity.ImageDiff" -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

namespace Phlosion.Tools.RenderParity
{
    public sealed class ImageDiffResult
    {
        public int Width { get; set; }
        public int Height { get; set; }
        public double MeanAbsoluteError { get; set; }
        public double RootMeanSquareError { get; set; }
        public double MaxChannelError { get; set; }
        public double ChangedPixelRatio { get; set; }
        public int PixelChannelTolerance { get; set; }
    }

    public static class ImageDiff
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

        private static byte ScaleDifference(int difference, int scale)
        {
            return (byte)Math.Min(255, difference * scale);
        }

        private static void WriteHeatmap(
            string path,
            byte[] reference,
            byte[] candidate,
            int width,
            int height,
            int scale)
        {
            using (Bitmap heatmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
            {
                Rectangle rect = new Rectangle(0, 0, width, height);
                BitmapData data = heatmap.LockBits(
                    rect,
                    ImageLockMode.WriteOnly,
                    PixelFormat.Format32bppArgb);
                try
                {
                    int stride = Math.Abs(data.Stride);
                    int rowBytes = width * 4;
                    byte[] output = new byte[stride * height];
                    for (int y = 0; y < height; ++y)
                    {
                        int destinationRow = data.Stride >= 0 ? y : height - 1 - y;
                        int destinationOffset = destinationRow * stride;
                        int sourceOffset = y * rowBytes;
                        for (int x = 0; x < width; ++x)
                        {
                            int sourcePixel = sourceOffset + x * 4;
                            int destinationPixel = destinationOffset + x * 4;
                            output[destinationPixel + 0] = ScaleDifference(
                                Math.Abs(reference[sourcePixel + 0] - candidate[sourcePixel + 0]),
                                scale);
                            output[destinationPixel + 1] = ScaleDifference(
                                Math.Abs(reference[sourcePixel + 1] - candidate[sourcePixel + 1]),
                                scale);
                            output[destinationPixel + 2] = ScaleDifference(
                                Math.Abs(reference[sourcePixel + 2] - candidate[sourcePixel + 2]),
                                scale);
                            output[destinationPixel + 3] = 255;
                        }
                    }
                    Marshal.Copy(output, 0, data.Scan0, output.Length);
                }
                finally
                {
                    heatmap.UnlockBits(data);
                }

                string parent = Path.GetDirectoryName(path);
                if (!String.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);
                heatmap.Save(path, ImageFormat.Png);
            }
        }

        public static ImageDiffResult Compare(
            string referencePath,
            string candidatePath,
            string heatmapPath,
            int pixelChannelTolerance,
            int heatmapScale)
        {
            using (Bitmap referenceBitmap = LoadNormalized(referencePath))
            using (Bitmap candidateBitmap = LoadNormalized(candidatePath))
            {
                if (referenceBitmap.Width != candidateBitmap.Width ||
                    referenceBitmap.Height != candidateBitmap.Height)
                {
                    throw new InvalidOperationException(String.Format(
                        "Image size mismatch: reference={0}x{1} candidate={2}x{3}",
                        referenceBitmap.Width,
                        referenceBitmap.Height,
                        candidateBitmap.Width,
                        candidateBitmap.Height));
                }

                byte[] reference = ReadPixels(referenceBitmap);
                byte[] candidate = ReadPixels(candidateBitmap);
                double absoluteSum = 0.0;
                double squaredSum = 0.0;
                int maximum = 0;
                long changedPixels = 0;
                long pixelCount = (long)referenceBitmap.Width * referenceBitmap.Height;

                for (int pixel = 0; pixel < reference.Length; pixel += 4)
                {
                    int pixelMaximum = 0;
                    for (int channel = 0; channel < 3; ++channel)
                    {
                        int difference = Math.Abs(reference[pixel + channel] - candidate[pixel + channel]);
                        absoluteSum += difference;
                        squaredSum += difference * difference;
                        pixelMaximum = Math.Max(pixelMaximum, difference);
                        maximum = Math.Max(maximum, difference);
                    }
                    if (pixelMaximum > pixelChannelTolerance) ++changedPixels;
                }

                if (!String.IsNullOrEmpty(heatmapPath))
                {
                    WriteHeatmap(
                        heatmapPath,
                        reference,
                        candidate,
                        referenceBitmap.Width,
                        referenceBitmap.Height,
                        Math.Max(1, heatmapScale));
                }

                double channelCount = Math.Max(1.0, pixelCount * 3.0);
                return new ImageDiffResult
                {
                    Width = referenceBitmap.Width,
                    Height = referenceBitmap.Height,
                    MeanAbsoluteError = absoluteSum / channelCount / 255.0,
                    RootMeanSquareError = Math.Sqrt(squaredSum / channelCount) / 255.0,
                    MaxChannelError = maximum / 255.0,
                    ChangedPixelRatio = changedPixels / Math.Max(1.0, pixelCount),
                    PixelChannelTolerance = pixelChannelTolerance
                };
            }
        }
    }
}
'@ -ReferencedAssemblies System.Drawing
}

function Compare-RenderParityImages {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ReferencePath,
        [Parameter(Mandatory = $true)]
        [string]$CandidatePath,
        [string]$HeatmapPath,
        [ValidateRange(0, 255)]
        [int]$PixelChannelTolerance = 8,
        [ValidateRange(1, 32)]
        [int]$HeatmapScale = 4
    )

    return [Phlosion.Tools.RenderParity.ImageDiff]::Compare(
        $ReferencePath,
        $CandidatePath,
        $HeatmapPath,
        $PixelChannelTolerance,
        $HeatmapScale)
}

Export-ModuleMember -Function Compare-RenderParityImages
