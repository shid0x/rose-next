using System;
using System.IO;

namespace Map_Editor.Engine.Minimap
{
    /// <summary>
    /// Reads and writes the DDS files minimaps live in, on the CPU: reading
    /// covers DXT1/3/5 and uncompressed 24/32-bit (every minimap and ocean
    /// texture we have seen), writing is DXT5 with a full mip chain and the
    /// legacy DX9 header, as retail minimaps are stored. Pixels are RGB bytes,
    /// top row first.
    /// </summary>
    public static class DdsCodec
    {
        /// <summary>Width and height from a DDS header, without decoding.</summary>
        public static bool ReadSize(byte[] dds, out int width, out int height)
        {
            width = height = 0;
            if (dds == null || dds.Length < 128 || dds[0] != 'D' || dds[1] != 'D' || dds[2] != 'S' || dds[3] != ' ')
                return false;

            height = BitConverter.ToInt32(dds, 12);
            width = BitConverter.ToInt32(dds, 16);
            return width > 0 && height > 0;
        }

        /// <summary>The top mip level as RGB bytes.</summary>
        public static byte[] Decode(byte[] dds, out int width, out int height)
        {
            if (!ReadSize(dds, out width, out height))
                throw new InvalidDataException("not a DDS file");

            int pfFlags = BitConverter.ToInt32(dds, 80);
            string fourCC = System.Text.Encoding.ASCII.GetString(dds, 84, 4);
            int bits = BitConverter.ToInt32(dds, 88);
            uint rMask = BitConverter.ToUInt32(dds, 92), gMask = BitConverter.ToUInt32(dds, 96), bMask = BitConverter.ToUInt32(dds, 100);
            byte[] rgb = new byte[width * height * 3];
            int offset = 128;

            if ((pfFlags & 0x4) != 0)
            {
                int mode = fourCC == "DXT1" ? 1 : fourCC == "DXT3" ? 3 : fourCC == "DXT5" ? 5 : 0;
                if (mode == 0)
                    throw new InvalidDataException("unsupported DDS format " + fourCC);

                int blockSize = mode == 1 ? 8 : 16;
                int bw = (width + 3) / 4, bh = (height + 3) / 4;
                byte[] colours = new byte[16 * 3];

                for (int by = 0; by < bh; by++)
                {
                    for (int bx = 0; bx < bw; bx++)
                    {
                        int block = offset + (by * bw + bx) * blockSize + (mode == 1 ? 0 : 8);
                        DecodeColourBlock(dds, block, mode == 1, colours);

                        for (int py = 0; py < 4; py++)
                        {
                            int y = by * 4 + py;
                            if (y >= height)
                                break;

                            for (int px = 0; px < 4; px++)
                            {
                                int x = bx * 4 + px;
                                if (x >= width)
                                    break;

                                int s = (py * 4 + px) * 3, d = (y * width + x) * 3;
                                rgb[d] = colours[s];
                                rgb[d + 1] = colours[s + 1];
                                rgb[d + 2] = colours[s + 2];
                            }
                        }
                    }
                }
                return rgb;
            }

            if (bits != 24 && bits != 32)
                throw new InvalidDataException("unsupported DDS pixel size " + bits);

            int step = bits / 8;
            int pitch = width * step;
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    int s = offset + y * pitch + x * step;
                    uint v = step == 4 ? BitConverter.ToUInt32(dds, s) : (uint)(dds[s] | dds[s + 1] << 8 | dds[s + 2] << 16);
                    int d = (y * width + x) * 3;
                    rgb[d] = Channel(v, rMask);
                    rgb[d + 1] = Channel(v, gMask);
                    rgb[d + 2] = Channel(v, bMask);
                }
            }
            return rgb;
        }

        private static byte Channel(uint v, uint mask)
        {
            if (mask == 0)
                return 0;

            int shift = 0;
            while (((mask >> shift) & 1) == 0)
                shift++;

            uint max = mask >> shift;
            return (byte)(((v & mask) >> shift) * 255 / max);
        }

        private static void Unpack565(int c, out int r, out int g, out int b)
        {
            r = (c >> 11) & 31;
            g = (c >> 5) & 63;
            b = c & 31;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
        }

        private static void DecodeColourBlock(byte[] data, int o, bool dxt1, byte[] outColours)
        {
            int c0 = data[o] | data[o + 1] << 8, c1 = data[o + 2] | data[o + 3] << 8;
            int[] pr = new int[4], pg = new int[4], pb = new int[4];
            Unpack565(c0, out pr[0], out pg[0], out pb[0]);
            Unpack565(c1, out pr[1], out pg[1], out pb[1]);

            if (!dxt1 || c0 > c1)
            {
                pr[2] = (2 * pr[0] + pr[1]) / 3; pg[2] = (2 * pg[0] + pg[1]) / 3; pb[2] = (2 * pb[0] + pb[1]) / 3;
                pr[3] = (pr[0] + 2 * pr[1]) / 3; pg[3] = (pg[0] + 2 * pg[1]) / 3; pb[3] = (pb[0] + 2 * pb[1]) / 3;
            }
            else
            {
                pr[2] = (pr[0] + pr[1]) / 2; pg[2] = (pg[0] + pg[1]) / 2; pb[2] = (pb[0] + pb[1]) / 2;
                pr[3] = pg[3] = pb[3] = 0;
            }

            uint indices = BitConverter.ToUInt32(data, o + 4);
            for (int i = 0; i < 16; i++)
            {
                int k = (int)((indices >> (2 * i)) & 3);
                outColours[i * 3] = (byte)pr[k];
                outColours[i * 3 + 1] = (byte)pg[k];
                outColours[i * 3 + 2] = (byte)pb[k];
            }
        }

        /// <summary>
        /// RGB bytes -> a DXT5 DDS (opaque) with every mip level down to 1x1.
        /// Sides need not be powers of two: each level halves, rounding down,
        /// as texconv and retail do (384 -> 9 levels, 512x384 -> 10).
        /// </summary>
        public static byte[] EncodeDxt5(byte[] rgb, int width, int height)
        {
            MemoryStream ms = new MemoryStream();
            BinaryWriter w = new BinaryWriter(ms);

            int levels = 1;
            for (int lw = width, lh = height; lw > 1 || lh > 1; levels++)
            {
                lw = Math.Max(1, lw / 2);
                lh = Math.Max(1, lh / 2);
            }

            w.Write(new byte[] { (byte)'D', (byte)'D', (byte)'S', (byte)' ' });
            w.Write(124);
            w.Write(0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);   // caps height width pixelformat mipmapcount linearsize
            w.Write(height);
            w.Write(width);
            w.Write(Math.Max(1, (width + 3) / 4) * Math.Max(1, (height + 3) / 4) * 16);
            w.Write(0);
            w.Write(levels);
            for (int i = 0; i < 11; i++)
                w.Write(0);
            w.Write(32);
            w.Write(0x4);                                               // DDPF_FOURCC
            w.Write(new byte[] { (byte)'D', (byte)'X', (byte)'T', (byte)'5' });
            for (int i = 0; i < 5; i++)
                w.Write(0);
            w.Write(0x1000 | 0x8 | 0x400000);                           // texture, complex, mipmap
            for (int i = 0; i < 4; i++)
                w.Write(0);

            byte[] level = rgb;
            int cw = width, ch = height;
            for (int l = 0; l < levels; l++)
            {
                WriteDxt5Level(w, level, cw, ch);
                if (l + 1 < levels)
                {
                    int nw = Math.Max(1, cw / 2), nh = Math.Max(1, ch / 2);
                    level = Halve(level, cw, ch, nw, nh);
                    cw = nw;
                    ch = nh;
                }
            }

            w.Flush();
            return ms.ToArray();
        }

        private static byte[] Halve(byte[] src, int w, int h, int nw, int nh)
        {
            byte[] dst = new byte[nw * nh * 3];
            for (int y = 0; y < nh; y++)
            {
                int y0 = Math.Min(h - 1, y * 2), y1 = Math.Min(h - 1, y * 2 + 1);
                for (int x = 0; x < nw; x++)
                {
                    int x0 = Math.Min(w - 1, x * 2), x1 = Math.Min(w - 1, x * 2 + 1);
                    for (int c = 0; c < 3; c++)
                    {
                        int sum = src[(y0 * w + x0) * 3 + c] + src[(y0 * w + x1) * 3 + c] + src[(y1 * w + x0) * 3 + c] + src[(y1 * w + x1) * 3 + c];
                        dst[(y * nw + x) * 3 + c] = (byte)((sum + 2) / 4);
                    }
                }
            }
            return dst;
        }

        private static void WriteDxt5Level(BinaryWriter w, byte[] rgb, int width, int height)
        {
            int bw = Math.Max(1, (width + 3) / 4), bh = Math.Max(1, (height + 3) / 4);
            float[] block = new float[48];
            for (int by = 0; by < bh; by++)
            {
                for (int bx = 0; bx < bw; bx++)
                {
                    for (int i = 0; i < 16; i++)
                    {
                        int x = Math.Min(width - 1, bx * 4 + i % 4), y = Math.Min(height - 1, by * 4 + i / 4);
                        int s = (y * width + x) * 3;
                        block[i * 3] = rgb[s];
                        block[i * 3 + 1] = rgb[s + 1];
                        block[i * 3 + 2] = rgb[s + 2];
                    }

                    // alpha: opaque, both endpoints 255, every index 0
                    w.Write((byte)255);
                    w.Write((byte)255);
                    w.Write(new byte[6]);
                    WriteColourBlock(w, block);
                }
            }
        }

        private static int Pack565(float r, float g, float b)
        {
            int ri = Math.Max(0, Math.Min(31, (int)Math.Round(r * 31.0f / 255.0f)));
            int gi = Math.Max(0, Math.Min(63, (int)Math.Round(g * 63.0f / 255.0f)));
            int bi = Math.Max(0, Math.Min(31, (int)Math.Round(b * 31.0f / 255.0f)));
            return (ri << 11) | (gi << 5) | bi;
        }

        /// <summary>
        /// One colour block: the end points are the extremes of the pixels
        /// along their principal axis, pulled in slightly; each pixel takes
        /// the nearest of the four palette colours.
        /// </summary>
        private static void WriteColourBlock(BinaryWriter w, float[] p)
        {
            float mr = 0, mg = 0, mb = 0;
            for (int i = 0; i < 16; i++)
            {
                mr += p[i * 3]; mg += p[i * 3 + 1]; mb += p[i * 3 + 2];
            }
            mr /= 16; mg /= 16; mb /= 16;

            float rr = 0, rg = 0, rb = 0, gg = 0, gb = 0, bb = 0;
            for (int i = 0; i < 16; i++)
            {
                float r = p[i * 3] - mr, g = p[i * 3 + 1] - mg, b = p[i * 3 + 2] - mb;
                rr += r * r; rg += r * g; rb += r * b; gg += g * g; gb += g * b; bb += b * b;
            }

            float ar = 1, ag = 1, ab = 1;
            for (int it = 0; it < 8; it++)
            {
                float nr = rr * ar + rg * ag + rb * ab, ng = rg * ar + gg * ag + gb * ab, nb = rb * ar + gb * ag + bb * ab;
                float len = (float)Math.Sqrt(nr * nr + ng * ng + nb * nb);
                if (len < 1e-6f)
                    break;
                ar = nr / len; ag = ng / len; ab = nb / len;
            }

            float lo = float.MaxValue, hi = float.MinValue;
            for (int i = 0; i < 16; i++)
            {
                float t = (p[i * 3] - mr) * ar + (p[i * 3 + 1] - mg) * ag + (p[i * 3 + 2] - mb) * ab;
                lo = Math.Min(lo, t);
                hi = Math.Max(hi, t);
            }
            float inset = (hi - lo) / 32.0f;
            lo += inset;
            hi -= inset;

            int c0 = Pack565(mr + ar * hi, mg + ag * hi, mb + ab * hi);
            int c1 = Pack565(mr + ar * lo, mg + ag * lo, mb + ab * lo);
            if (c0 < c1)
            {
                int t = c0; c0 = c1; c1 = t;
            }

            uint indices = 0;
            if (c0 != c1)
            {
                int[] pr = new int[4], pg = new int[4], pb = new int[4];
                Unpack565(c0, out pr[0], out pg[0], out pb[0]);
                Unpack565(c1, out pr[1], out pg[1], out pb[1]);
                pr[2] = (2 * pr[0] + pr[1]) / 3; pg[2] = (2 * pg[0] + pg[1]) / 3; pb[2] = (2 * pb[0] + pb[1]) / 3;
                pr[3] = (pr[0] + 2 * pr[1]) / 3; pg[3] = (pg[0] + 2 * pg[1]) / 3; pb[3] = (pb[0] + 2 * pb[1]) / 3;

                for (int i = 0; i < 16; i++)
                {
                    int best = 0;
                    float bestD = float.MaxValue;
                    for (int k = 0; k < 4; k++)
                    {
                        float dr = p[i * 3] - pr[k], dg = p[i * 3 + 1] - pg[k], db = p[i * 3 + 2] - pb[k];
                        float d = dr * dr + dg * dg + db * db;
                        if (d < bestD)
                        {
                            bestD = d;
                            best = k;
                        }
                    }
                    indices |= (uint)best << (2 * i);
                }
            }

            w.Write((ushort)c0);
            w.Write((ushort)c1);
            w.Write(indices);
        }
    }
}
