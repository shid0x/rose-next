using System;
using System.Collections.Generic;
using System.IO;
using Map_Editor.Engine;
using Map_Editor.Engine.Data;
using Map_Editor.Engine.Minimap;

/// <summary>
/// Tools > Make minimap, without the GPU: the DDS codec against real
/// minimaps, and Install / Restore on a scratch copy of the data (a zone with
/// no minimap, which also fills LIST_ZONE, and a zone with one). Run by
/// Run-MinimapTests.ps1.
/// </summary>
public static class MinimapTests
{
    private static int failures;

    private static void Check(bool ok, string what)
    {
        Console.WriteLine((ok ? "  ok    " : "  FAIL  ") + what);
        if (!ok)
            failures++;
    }

    public static int Main(string[] args)
    {
        string data = args[0], scratch = args[1];

        Console.WriteLine("DDS codec");
        foreach (string rel in new string[] { @"3DDATA\MAPS\JUNON\JG01\MINIMAP.DDS", @"3DDATA\MAPS\JUNON\JD01\MINIMAP.DDS", @"3DDATA\MAPS\ELDEON\EJ01\MINIMAP.DDS" })
        {
            int w, h, w2, h2;
            byte[] rgb = DdsCodec.Decode(File.ReadAllBytes(Path.Combine(data, rel)), out w, out h);
            byte[] dds = DdsCodec.EncodeDxt5(rgb, w, h);
            byte[] back = DdsCodec.Decode(dds, out w2, out h2);
            int mips = BitConverter.ToInt32(dds, 28), expected = 1;
            for (int a = w, b = h; a > 1 || b > 1; expected++) { a = Math.Max(1, a / 2); b = Math.Max(1, b / 2); }
            int size = 128;
            for (int l = 0, a = w, b = h; l < mips; l++, a = Math.Max(1, a / 2), b = Math.Max(1, b / 2))
                size += Math.Max(1, (a + 3) / 4) * Math.Max(1, (b + 3) / 4) * 16;
            double err = 0;
            for (int i = 0; i < rgb.Length; i++) err += Math.Abs(rgb[i] - back[i]);
            err /= rgb.Length;
            File.WriteAllBytes(Path.Combine(scratch, Path.GetFileName(Path.GetDirectoryName(rel)) + ".rgb"), rgb);
            Check(w2 == w && h2 == h, string.Format("{0}: {1}x{2} survives", rel, w, h));
            Check(mips == expected && dds.Length == size, string.Format("{0}: {1} mip levels, {2} bytes", rel, mips, dds.Length));
            Check(err < 4.0, string.Format("{0}: DXT5 error {1:0.00} per channel", rel, err));
        }

        if (args.Length > 2)
        {
            // a fresh picture (an editor-made minimap), not one that was DXT already
            using (System.Drawing.Bitmap bmp = new System.Drawing.Bitmap(args[2]))
            {
                int w = bmp.Width, h = bmp.Height, w2, h2;
                byte[] rgb = new byte[w * h * 3];
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                    {
                        System.Drawing.Color c = bmp.GetPixel(x, y);
                        rgb[(y * w + x) * 3] = c.R; rgb[(y * w + x) * 3 + 1] = c.G; rgb[(y * w + x) * 3 + 2] = c.B;
                    }
                byte[] back = DdsCodec.Decode(DdsCodec.EncodeDxt5(rgb, w, h), out w2, out h2);
                double err = 0;
                for (int i = 0; i < rgb.Length; i++) err += Math.Abs(rgb[i] - back[i]);
                err /= rgb.Length;
                Check(err < 4.0, string.Format("{0}: fresh picture, DXT5 error {1:0.00} per channel", Path.GetFileName(args[2]), err));
            }
        }

        Console.WriteLine("Install / Restore");
        string root = Path.Combine(scratch, "data");
        if (Directory.Exists(root))
            Directory.Delete(root, true);
        Copy(data, root, @"3DDATA\STB\LIST_ZONE.STB");
        foreach (string folder in new string[] { @"3DDATA\MAPS\JUNON\MAPGEN10", @"3DDATA\MAPS\JUNON\JG01" })
            foreach (string f in Directory.GetFiles(Path.Combine(data, folder), "*.HIM"))
                Copy(data, root, Path.Combine(folder, Path.GetFileName(f)));
        Copy(data, root, @"3DDATA\MAPS\JUNON\JG01\MINIMAP.DDS");
        Directory.SetCurrentDirectory(root);

        byte[] stbBefore = File.ReadAllBytes(@"3DDATA\STB\LIST_ZONE.STB");
        FileManager.STBs = new Dictionary<string, STB>();
        FileManager.STBs["LIST_ZONE"] = new STB(@"3DDATA\STB\LIST_ZONE.STB");

        // a zone with no minimap (MAPGEN10, zone 12)
        MinimapZone z = MinimapZone.ForZone(12);
        Check(!z.HasMinimap && z.Width == 384 && z.Height == 384 && z.StartX == 30 && z.StartY == 31,
              string.Format("zone 12 extent from its chunks: {0}x{1} at {2}_{3}", z.Width, z.Height, z.StartX, z.StartY));
        byte[] picture = new byte[z.Width * z.Height * 3];
        for (int i = 0; i < picture.Length; i++) picture[i] = (byte)(i * 7 % 251);
        z.Install(picture);
        STB after = new STB(@"3DDATA\STB\LIST_ZONE.STB");
        Check(File.Exists(z.TargetPath), "zone 12: " + z.TargetPath + " written");
        Check(after.Cells[12][9] == z.TargetPath && after.Cells[12][10] == "30" && after.Cells[12][11] == "31",
              string.Format("zone 12: LIST_ZONE cols 8-10 = {0} {1} {2}", after.Cells[12][9], after.Cells[12][10], after.Cells[12][11]));
        int elsewhere = 0;
        for (int r = 0; r < after.Cells.Count; r++)
            for (int c = 0; c < after.Cells[r].Count; c++)
                if (after.Cells[r][c] != new STB_Before().Get(stbBefore, r, c) && !(r == 12 && c >= 9 && c <= 11))
                    elsewhere++;
        Check(elsewhere == 0, string.Format("zone 12: no other LIST_ZONE cell changed ({0})", elsewhere));
        MinimapZone again = MinimapZone.ForZone(12);
        Check(again.HasMinimap && again.Width == 384, "zone 12: now has a 384x384 minimap");
        again.Restore();
        Check(Same(File.ReadAllBytes(@"3DDATA\STB\LIST_ZONE.STB"), stbBefore), "zone 12: Restore gives LIST_ZONE back byte for byte");
        Check(!File.Exists(z.TargetPath) && !Directory.Exists(z.BackupDirectory), "zone 12: Restore removes the DDS and the backup");

        // a zone with a minimap (JG01, zone 22)
        byte[] ddsBefore = File.ReadAllBytes(@"3DDATA\MAPS\JUNON\JG01\MINIMAP.DDS");
        MinimapZone j = MinimapZone.ForZone(22);
        Check(j.HasMinimap && j.Width == 512 && j.Height == 384, string.Format("zone 22 keeps its minimap's extent: {0}x{1}", j.Width, j.Height));
        byte[] p2 = new byte[j.Width * j.Height * 3];
        j.Install(p2);
        Check(!Same(File.ReadAllBytes(j.TargetPath), ddsBefore), "zone 22: minimap replaced");
        Check(Same(File.ReadAllBytes(@"3DDATA\STB\LIST_ZONE.STB"), stbBefore), "zone 22: LIST_ZONE untouched");
        int ow, oh;
        byte[] orig = MinimapZone.ReadOriginal(j.MinimapPath, out ow, out oh);
        int dw, dh;
        Check(orig != null && Same(orig, DdsCodec.Decode(ddsBefore, out dw, out dh)), "zone 22: the original is still readable from the backup");
        j.Install(p2);
        j.Restore();
        Check(Same(File.ReadAllBytes(j.TargetPath), ddsBefore), "zone 22: Restore after two installs gives the first original back");

        Console.WriteLine("Window");
        Exception uiError = null;
        System.Threading.Thread ui = new System.Threading.Thread(delegate()
        {
            try
            {
                // the dialog's XAML loads, and its layout as a picture (never shown: no map behind it)
                Map_Editor.Forms.Tools.MinimapWindow window = new Map_Editor.Forms.Tools.MinimapWindow();
                System.Windows.FrameworkElement content = (System.Windows.FrameworkElement)window.Content;
                window.Content = null;
                System.Windows.Controls.Border host = new System.Windows.Controls.Border { Background = window.Background, Resources = window.Resources, Child = content };
                host.Measure(new System.Windows.Size(1104, 642));
                host.Arrange(new System.Windows.Rect(0, 0, 1104, 642));
                System.Windows.Media.Imaging.RenderTargetBitmap shot = new System.Windows.Media.Imaging.RenderTargetBitmap(1104, 642, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
                shot.Render(host);
                System.Windows.Media.Imaging.PngBitmapEncoder png = new System.Windows.Media.Imaging.PngBitmapEncoder();
                png.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(shot));
                using (FileStream fs = File.Create(Path.Combine(scratch, "window.png")))
                    png.Save(fs);
            }
            catch (Exception e)
            {
                uiError = e;
            }
        });
        ui.SetApartmentState(System.Threading.ApartmentState.STA);
        ui.Start();
        ui.Join();
        Check(uiError == null, "MinimapWindow builds from its XAML" + (uiError != null ? ": " + uiError : " (layout in window.png)"));

        Console.WriteLine(failures == 0 ? "all passed" : failures + " failed");
        return failures == 0 ? 0 : 1;
    }

    /// <summary>Reads a cell of the untouched table (the STB class reads from a path).</summary>
    private class STB_Before
    {
        private static STB cached;
        public string Get(byte[] raw, int r, int c)
        {
            if (cached == null)
            {
                string tmp = Path.GetTempFileName();
                File.WriteAllBytes(tmp, raw);
                cached = new STB(tmp);
                File.Delete(tmp);
            }
            return cached.Cells[r][c];
        }
    }

    private static void Copy(string from, string to, string rel)
    {
        string dst = Path.Combine(to, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(dst));
        File.Copy(Path.Combine(from, rel), dst, true);
    }

    private static bool Same(byte[] a, byte[] b)
    {
        if (a.Length != b.Length)
            return false;
        for (int i = 0; i < a.Length; i++)
            if (a[i] != b[i])
                return false;
        return true;
    }
}
