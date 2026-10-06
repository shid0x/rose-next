using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;

namespace Map_Editor.Engine.Minimap
{
    public class MinimapGate
    {
        public float X, Y;      // editor metres
        public string Name;
    }

    public class MinimapStyleInput
    {
        public MinimapRender Render;
        public float X0, YTop;
        public byte[] Original;             // the zone's minimap, same size, or null
        public byte[] Frame;                // a retail minimap to cut the frame from, or null
        public int FrameWidth, FrameHeight;
        public byte[] Ocean;                // the game's ocean texture, or null
        public int OceanWidth, OceanHeight;
        public List<MinimapGate> Gates = new List<MinimapGate>();
        public bool MatchOriginal = true;
        public bool Labels = true;
        public bool WaterTexture = true;
    }

    public class MinimapResult
    {
        public int Width, Height;
        public byte[] Rgb;
        public double Difference = -1;      // mean abs per channel inside the frame vs Original
        public List<string> Notes = new List<string>();
    }

    /// <summary>
    /// Turns a capture into a retail-looking minimap. The numbers were
    /// measured against eight retail minimaps rendered over their exact extent
    /// (scripts/make-minimap.py, 2026-10-06): the land needs no grade and no
    /// relief shading (retail is the plain lightmapped render); only the water
    /// was painted by hand -- flat blue, darker away from the shore. When the
    /// zone already has a minimap, the land grade and the water colours are
    /// fitted to it, so a remade map keeps its zone's look.
    /// </summary>
    public static class MinimapStyle
    {
        public const int FRAME = MinimapZone.PX_PER_CHUNK;

        private static readonly float[] WATER_SHORE = { 72, 112, 150 };
        private static readonly float[] WATER_DEEP = { 74, 138, 228 };
        private const float WATER_DEPTH_M = 30.0f;
        private const float WATER_TEXTURE = 0.08f;
        private const int WATER_TEXTURE_PX = 48;

        public static MinimapResult Make(MinimapStyleInput input)
        {
            MinimapRender r = input.Render;
            int W = r.Width, H = r.Height, n = W * H;
            MinimapResult result = new MinimapResult { Width = W, Height = H };

            bool[] isLand = new bool[n];
            for (int i = 0; i < n; i++)
                isLand[i] = r.LandCover[i] >= 0.5f;
            float[] dist = DistanceTo(isLand, W, H);             // pixels to the nearest land pixel

            float[] land = r.Land;
            float[] shore = WATER_SHORE, deep = WATER_DEEP;
            if (input.MatchOriginal && input.Original != null)
            {
                float[] fitted = FitLand(input.Original, r, W, H);
                if (fitted != null)
                {
                    land = fitted;
                    result.Notes.Add("land colours fitted to the current minimap");
                }
                float[] s, d;
                if (FitWater(input.Original, r, dist, W, H, out s, out d))
                {
                    shore = s;
                    deep = d;
                    result.Notes.Add(string.Format("water from the current minimap: shore {0},{1},{2}, open {3},{4},{5}",
                                                   s[0], s[1], s[2], d[0], d[1], d[2]));
                }
            }

            float[] water = WaterColour(dist, W, H, shore, deep, input.WaterTexture ? input : null);

            float[] img = new float[n * 3];
            bool[] solid = new bool[n];
            bool anySolid = false, anyVoid = false;
            for (int i = 0; i < n; i++)
            {
                float lc = r.LandCover[i], wc = r.WaterCover[i], s = lc + wc;
                solid[i] = s > 0.5f;
                anySolid |= solid[i];
                anyVoid |= !solid[i];
                if (s <= 0)
                    continue;
                for (int c = 0; c < 3; c++)
                    img[i * 3 + c] = (land[i * 3 + c] * lc + water[i * 3 + c] * wc) / s;
            }

            if (anyVoid && anySolid)
            {
                FillVoid(img, land, water, r, solid, W, H);
                result.Notes.Add("chunks the map does not have are filled from their surroundings");
            }

            Frame(img, W, H, input);

            byte[] rgb = new byte[n * 3];
            for (int i = 0; i < rgb.Length; i++)
                rgb[i] = (byte)Math.Max(0, Math.Min(255, (int)Math.Round(img[i])));

            if (input.Labels)
                DrawLabels(rgb, W, H, input);

            result.Rgb = rgb;
            if (input.Original != null)
            {
                double sum = 0;
                int count = 0;
                for (int y = FRAME; y < H - FRAME; y++)
                    for (int x = FRAME; x < W - FRAME; x++)
                        for (int c = 0; c < 3; c++, count++)
                            sum += Math.Abs(rgb[(y * W + x) * 3 + c] - input.Original[(y * W + x) * 3 + c]);
                result.Difference = count > 0 ? sum / count : -1;
            }
            return result;
        }

        // ------------------------------------------------------------------ water

        private static float[] WaterColour(float[] dist, int W, int H, float[] shore, float[] deep, MinimapStyleInput texture)
        {
            float[] tex = null;
            int tn = WATER_TEXTURE_PX;
            if (texture != null && texture.Ocean != null)
                tex = Normalised(Grey(texture.Ocean, texture.OceanWidth, texture.OceanHeight, tn));

            float[] col = new float[W * H * 3];
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    int i = y * W + x;
                    float t = Math.Max(0.0f, Math.Min(1.0f, dist[i] * MinimapZone.M_PER_PX / WATER_DEPTH_M));
                    t = t * t * (3 - 2 * t);
                    float m = tex != null ? 1.0f + WATER_TEXTURE * 0.25f * tex[(y % tn) * tn + x % tn] : 1.0f;
                    for (int c = 0; c < 3; c++)
                        col[i * 3 + c] = (shore[c] * (1 - t) + deep[c] * t) * m;
                }
            }
            return col;
        }

        private static float[] Grey(byte[] rgb, int w, int h, int n)
        {
            float[] g = new float[n * n];
            for (int y = 0; y < n; y++)
            {
                int y0 = y * h / n, y1 = Math.Max(y0 + 1, (y + 1) * h / n);
                for (int x = 0; x < n; x++)
                {
                    int x0 = x * w / n, x1 = Math.Max(x0 + 1, (x + 1) * w / n);
                    float sum = 0;
                    for (int sy = y0; sy < y1; sy++)
                        for (int sx = x0; sx < x1; sx++)
                        {
                            int s = (sy * w + sx) * 3;
                            sum += 0.299f * rgb[s] + 0.587f * rgb[s + 1] + 0.114f * rgb[s + 2];
                        }
                    g[y * n + x] = sum / ((y1 - y0) * (x1 - x0));
                }
            }
            return g;
        }

        private static float[] Normalised(float[] a)
        {
            double mean = 0, var = 0;
            foreach (float v in a)
                mean += v;
            mean /= a.Length;
            foreach (float v in a)
                var += (v - mean) * (v - mean);
            float sd = (float)Math.Max(1e-3, Math.Sqrt(var / a.Length));
            float[] o = new float[a.Length];
            for (int i = 0; i < a.Length; i++)
                o[i] = (float)(a[i] - mean) / sd;
            return o;
        }

        /// <summary>Exact Euclidean distance (px) to the nearest true pixel (Felzenszwalb).</summary>
        private static float[] DistanceTo(bool[] mask, int W, int H)
        {
            const double INF = 1e20;
            double[] f = new double[W * H];
            for (int i = 0; i < f.Length; i++)
                f[i] = mask[i] ? 0 : INF;

            int m = Math.Max(W, H);
            double[] line = new double[m], d = new double[m], z = new double[m + 1];
            int[] v = new int[m];
            for (int x = 0; x < W; x++)
            {
                for (int y = 0; y < H; y++) line[y] = f[y * W + x];
                Edt1D(line, d, H, v, z);
                for (int y = 0; y < H; y++) f[y * W + x] = d[y];
            }
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++) line[x] = f[y * W + x];
                Edt1D(line, d, W, v, z);
                for (int x = 0; x < W; x++) f[y * W + x] = d[x];
            }

            float[] o = new float[W * H];
            for (int i = 0; i < o.Length; i++)
                o[i] = (float)Math.Sqrt(Math.Min(f[i], 1e12));
            return o;
        }

        private static void Edt1D(double[] f, double[] d, int n, int[] v, double[] z)
        {
            int k = 0;
            v[0] = 0;
            z[0] = double.NegativeInfinity;
            z[1] = double.PositiveInfinity;
            for (int q = 1; q < n; q++)
            {
                double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * q - 2.0 * v[k]);
                while (s <= z[k])                  // z[0] is -infinity: stops at k = 0
                {
                    k--;
                    s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * q - 2.0 * v[k]);
                }
                k++;
                v[k] = q;
                z[k] = s;
                z[k + 1] = double.PositiveInfinity;
            }
            k = 0;
            for (int q = 0; q < n; q++)
            {
                while (z[k + 1] < q)
                    k++;
                d[q] = (double)(q - v[k]) * (q - v[k]) + f[v[k]];
            }
        }

        // ------------------------------------------------------------------ matching

        private static bool Inner(int x, int y, int W, int H)
        {
            return x >= FRAME + 6 && x < W - FRAME - 6 && y >= FRAME + 6 && y < H - FRAME - 6;
        }

        /// <summary>
        /// An affine colour transform from the render's land to the current
        /// minimap, least squares over the land pixels inside the frame,
        /// refitted three times without the pixels it explains worst (labels,
        /// markers, areas edited since). Null when there is too little land.
        /// </summary>
        private static float[] FitLand(byte[] orig, MinimapRender r, int W, int H)
        {
            List<int> idx = new List<int>();
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    if (Inner(x, y, W, H) && r.LandCover[y * W + x] > 0.99f)
                        idx.Add(y * W + x);
            if (idx.Count < 2000)
                return null;

            bool[] keep = new bool[idx.Count];
            for (int i = 0; i < keep.Length; i++) keep[i] = true;
            double[,] M = null;
            float[] res = new float[idx.Count];
            for (int it = 0; it < 3; it++)
            {
                double[,] N = new double[4, 4], B = new double[4, 3];
                for (int i = 0; i < idx.Count; i++)
                {
                    if (!keep[i]) continue;
                    int p = idx[i];
                    double[] xv = { r.Land[p * 3], r.Land[p * 3 + 1], r.Land[p * 3 + 2], 1.0 };
                    for (int a = 0; a < 4; a++)
                    {
                        for (int b = 0; b < 4; b++) N[a, b] += xv[a] * xv[b];
                        for (int c = 0; c < 3; c++) B[a, c] += xv[a] * orig[p * 3 + c];
                    }
                }
                M = Solve(N, B);
                if (M == null)
                    return null;

                for (int i = 0; i < idx.Count; i++)
                {
                    int p = idx[i];
                    float e = 0;
                    for (int c = 0; c < 3; c++)
                        e += Math.Abs((float)Apply(M, r.Land, p, c) - orig[p * 3 + c]);
                    res[i] = e;
                }
                float[] sorted = (float[])res.Clone();
                Array.Sort(sorted);
                float limit = 2.5f * sorted[sorted.Length / 2] + 1;
                for (int i = 0; i < keep.Length; i++)
                    keep[i] = res[i] < limit;
            }

            float[] land = new float[W * H * 3];
            for (int p = 0; p < W * H; p++)
                for (int c = 0; c < 3; c++)
                    land[p * 3 + c] = (float)Apply(M, r.Land, p, c);
            return land;
        }

        private static double Apply(double[,] M, float[] land, int p, int c)
        {
            return land[p * 3] * M[0, c] + land[p * 3 + 1] * M[1, c] + land[p * 3 + 2] * M[2, c] + M[3, c];
        }

        /// <summary>Solves N X = B (4x4, 4x3) by Gaussian elimination with partial pivoting.</summary>
        private static double[,] Solve(double[,] N, double[,] B)
        {
            int n = 4, m = 3;
            double[,] a = new double[n, n + m];
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++) a[i, j] = N[i, j] + (i == j ? 1e-3 : 0);
                for (int j = 0; j < m; j++) a[i, n + j] = B[i, j];
            }
            for (int col = 0; col < n; col++)
            {
                int piv = col;
                for (int i = col + 1; i < n; i++)
                    if (Math.Abs(a[i, col]) > Math.Abs(a[piv, col])) piv = i;
                if (Math.Abs(a[piv, col]) < 1e-12)
                    return null;
                for (int j = 0; j < n + m; j++)
                {
                    double t = a[col, j]; a[col, j] = a[piv, j]; a[piv, j] = t;
                }
                for (int i = 0; i < n; i++)
                {
                    if (i == col) continue;
                    double f = a[i, col] / a[col, col];
                    for (int j = col; j < n + m; j++) a[i, j] -= f * a[col, j];
                }
            }
            double[,] x = new double[n, m];
            for (int i = 0; i < n; i++)
                for (int j = 0; j < m; j++)
                    x[i, j] = a[i, n + j] / a[i, i];
            return x;
        }

        /// <summary>The current minimap's water: the median colour at the shore (under 7.5 m) and in open water (20 m out).</summary>
        private static bool FitWater(byte[] orig, MinimapRender r, float[] dist, int W, int H, out float[] shore, out float[] deep)
        {
            List<int> near = new List<int>(), far = new List<int>();
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    int p = y * W + x;
                    if (!Inner(x, y, W, H) || r.WaterCover[p] <= 0.99f)
                        continue;
                    float m = dist[p] * MinimapZone.M_PER_PX;
                    if (m < 7.5f) near.Add(p);
                    else if (m >= 20.0f) far.Add(p);
                }
            }
            shore = deep = null;
            if (near.Count + far.Count < 500 || near.Count < 50 || far.Count < 50)
                return false;
            shore = Median(orig, near);
            deep = Median(orig, far);
            return true;
        }

        private static float[] Median(byte[] rgb, List<int> pixels)
        {
            float[] o = new float[3];
            int[] values = new int[pixels.Count];
            for (int c = 0; c < 3; c++)
            {
                for (int i = 0; i < pixels.Count; i++)
                    values[i] = rgb[pixels[i] * 3 + c];
                Array.Sort(values);
                o[c] = values[values.Length / 2];
            }
            return o;
        }

        // ------------------------------------------------------------------ void

        /// <summary>
        /// Pixels with no terrain (a chunk the map does not have) become more
        /// of their surroundings: water where the void is mostly bordered by
        /// water, the blended land colour elsewhere. Retail painted past the
        /// playable area; a grey hole reads as a bug.
        /// </summary>
        private static void FillVoid(float[] img, float[] land, float[] water, MinimapRender r, bool[] solid, int W, int H)
        {
            int n = W * H;
            bool[] isLand = new bool[n];
            float[] frac = new float[n];
            for (int i = 0; i < n; i++)
            {
                isLand[i] = r.LandCover[i] > 0.5f;
                float s = r.LandCover[i] + r.WaterCover[i];
                frac[i] = s > 0 ? r.WaterCover[i] / s : 0;
            }
            float[] landFill = PushPull(land, 3, isLand, W, H);
            float[] fracFill = PushPull(frac, 1, solid, W, H);
            for (int i = 0; i < n; i++)
            {
                if (solid[i])
                    continue;
                float t = Math.Max(0.0f, Math.Min(1.0f, (fracFill[i] - 0.35f) / 0.3f));
                t = t * t * (3 - 2 * t);
                for (int c = 0; c < 3; c++)
                    img[i * 3 + c] = landFill[i * 3 + c] * 0.94f * (1 - t) + water[i * 3 + c] * t;
            }
        }

        /// <summary>Fills the unknown pixels from a pyramid of averages of the known ones.</summary>
        private static float[] PushPull(float[] values, int C, bool[] known, int W, int H)
        {
            List<float[]> sums = new List<float[]>();
            List<float[]> weights = new List<float[]>();
            List<int> ws = new List<int>(), hs = new List<int>();

            float[] cs = new float[W * H * C], cw = new float[W * H];
            for (int i = 0; i < W * H; i++)
            {
                if (!known[i]) continue;
                cw[i] = 1;
                for (int c = 0; c < C; c++) cs[i * C + c] = values[i * C + c];
            }
            int w = W, h = H;
            while (true)
            {
                sums.Add(cs); weights.Add(cw); ws.Add(w); hs.Add(h);
                if (w <= 1 && h <= 1)
                    break;
                int w2 = (w + 1) / 2, h2 = (h + 1) / 2;
                float[] ns = new float[w2 * h2 * C], nw = new float[w2 * h2];
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                    {
                        int s = y * w + x, d = (y / 2) * w2 + x / 2;
                        nw[d] += cw[s];
                        for (int c = 0; c < C; c++) ns[d * C + c] += cs[s * C + c];
                    }
                cs = ns; cw = nw; w = w2; h = h2;
            }

            int last = sums.Count - 1;
            float[] fill = new float[ws[last] * hs[last] * C];
            for (int i = 0; i < ws[last] * hs[last]; i++)
                for (int c = 0; c < C; c++)
                    fill[i * C + c] = sums[last][i * C + c] / Math.Max(weights[last][i], 1e-6f);

            for (int l = last - 1; l >= 0; l--)
            {
                int fw = ws[l], fh = hs[l], kw = ws[l + 1], kh = hs[l + 1];
                float[] next = new float[fw * fh * C];
                for (int y = 0; y < fh; y++)
                {
                    float sy = Math.Max(0, Math.Min(kh - 1, (y + 0.5f) * kh / fh - 0.5f));
                    int y0 = (int)sy, y1 = Math.Min(kh - 1, y0 + 1);
                    float fy = sy - y0;
                    for (int x = 0; x < fw; x++)
                    {
                        float sx = Math.Max(0, Math.Min(kw - 1, (x + 0.5f) * kw / fw - 0.5f));
                        int x0 = (int)sx, x1 = Math.Min(kw - 1, x0 + 1);
                        float fx = sx - x0;
                        int i = y * fw + x;
                        float a = Math.Min(1.0f, weights[l][i]);
                        for (int c = 0; c < C; c++)
                        {
                            float up = (fill[(y0 * kw + x0) * C + c] * (1 - fx) + fill[(y0 * kw + x1) * C + c] * fx) * (1 - fy)
                                     + (fill[(y1 * kw + x0) * C + c] * (1 - fx) + fill[(y1 * kw + x1) * C + c] * fx) * fy;
                            float mean = weights[l][i] > 0 ? sums[l][i * C + c] / weights[l][i] : 0;
                            next[i * C + c] = mean * a + up * (1 - a);
                        }
                    }
                }
                fill = next;
            }
            return fill;
        }

        // ------------------------------------------------------------------ frame

        /// <summary>
        /// The 64 px parchment frame, cut to this size from a retail
        /// minimap's: corners as they are, edges lengthened by repeating whole
        /// periods of their ornament (so both ends still meet their corners);
        /// the top is the bottom mirrored, because retail labels usually sit on
        /// the top. A plain border when there is no source.
        /// </summary>
        private static void Frame(float[] img, int W, int H, MinimapStyleInput input)
        {
            const int F = FRAME;
            byte[] src = input.Frame;
            int sw = input.FrameWidth, sh = input.FrameHeight;
            if (src == null || sw < 3 * F || sh < 3 * F)
            {
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < W; x++)
                    {
                        int e = Math.Min(Math.Min(x, y), Math.Min(W - 1 - x, H - 1 - y));
                        if (e >= F) continue;
                        float k = e < 30 ? 0.55f : e > F - 6 ? 0.7f : 1.0f;
                        img[(y * W + x) * 3] = 196 * k; img[(y * W + x) * 3 + 1] = 160 * k; img[(y * W + x) * 3 + 2] = 112 * k;
                    }
                return;
            }

            Func<int, int, int, float> S = delegate(int x, int y, int c) { return src[(y * sw + x) * 3 + c]; };
            Action<int, int, int, float> D = delegate(int x, int y, int c, float v) { img[(y * W + x) * 3 + c] = v; };

            for (int y = 0; y < F; y++)
                for (int x = 0; x < F; x++)
                    for (int c = 0; c < 3; c++)
                    {
                        D(x, y, c, S(x, y, c));
                        D(W - F + x, y, c, S(sw - F + x, y, c));
                        D(x, H - F + y, c, S(x, sh - F + y, c));
                        D(W - F + x, H - F + y, c, S(sw - F + x, sh - F + y, c));
                    }

            // strips: [across (0 = against the map), along, channel]
            int bl = sw - 2 * F, vl = sh - 2 * F;
            float[,,] bottom = new float[F, bl, 3], left = new float[F, vl, 3], right = new float[F, vl, 3];
            for (int a = 0; a < F; a++)
                for (int c = 0; c < 3; c++)
                {
                    for (int t = 0; t < bl; t++) bottom[a, t, c] = S(F + t, sh - F + a, c);
                    for (int t = 0; t < vl; t++)
                    {
                        left[a, t, c] = S(F - 1 - a, F + t, c);
                        right[a, t, c] = S(sw - F + a, F + t, c);
                    }
                }

            float[,,] fb = FitEdge(bottom, W - 2 * F, Period(bottom));
            float[,,] fl = FitEdge(left, H - 2 * F, Period(left));
            float[,,] fr = FitEdge(right, H - 2 * F, Period(right));
            for (int a = 0; a < F; a++)
                for (int c = 0; c < 3; c++)
                {
                    for (int t = 0; t < W - 2 * F; t++)
                    {
                        D(F + t, H - F + a, c, fb[a, t, c]);
                        D(F + t, F - 1 - a, c, fb[a, t, c]);
                    }
                    for (int t = 0; t < H - 2 * F; t++)
                    {
                        D(F - 1 - a, F + t, c, fl[a, t, c]);
                        D(W - F + a, F + t, c, fr[a, t, c]);
                    }
                }
        }

        /// <summary>Repeat length of the ornament next to the map (autocorrelation of rows 2-11).</summary>
        private static int Period(float[,,] strip)
        {
            int L = strip.GetLength(1);
            float[] g = new float[L];
            float mean = 0;
            for (int t = 0; t < L; t++)
            {
                for (int a = 2; a < 12; a++)
                    for (int c = 0; c < 3; c++)
                        g[t] += strip[a, t, c];
                mean += g[t];
            }
            mean /= L;
            for (int t = 0; t < L; t++) g[t] -= mean;

            double zero = 0;
            for (int t = 0; t < L; t++) zero += g[t] * g[t];
            int best = 6;
            double bestV = double.MinValue;
            for (int lag = 6; lag < Math.Min(64, L); lag++)
            {
                double s = 0;
                for (int t = 0; t + lag < L; t++) s += g[t] * g[t + lag];
                if (s > bestV)
                {
                    bestV = s;
                    best = lag;
                }
            }
            return best;
        }

        private static float[,,] FitEdge(float[,,] strip, int length, int period)
        {
            int F = strip.GetLength(0), L = strip.GetLength(1);
            int s0 = Math.Min(24, L / 4);
            int s1 = s0 + Math.Max(1, (L - 2 * s0) / period) * period;
            if (s1 > L) s1 = L;
            int q = Math.Max(1, s1 - s0);
            int need = length - s0 - (L - s1);
            int n = Math.Max(0, (int)Math.Round((double)need / period) * period);

            List<int> cols = new List<int>();
            for (int t = 0; t < s0; t++) cols.Add(t);
            for (int i = 0; i < n; i++) cols.Add(s0 + i % q);
            for (int t = s1; t < L; t++) cols.Add(t);

            float[,,] o = new float[F, length, 3];
            int m = cols.Count;
            for (int t = 0; t < length; t++)
            {
                float st = m == length ? t : Math.Max(0, Math.Min(m - 1, (t + 0.5f) * m / length - 0.5f));
                int t0 = (int)st, t1 = Math.Min(m - 1, t0 + 1);
                float f = st - t0;
                for (int a = 0; a < F; a++)
                    for (int c = 0; c < 3; c++)
                        o[a, t, c] = strip[a, cols[t0], c] * (1 - f) + strip[a, cols[t1], c] * f;
            }
            return o;
        }

        // ------------------------------------------------------------------ labels

        /// <summary>
        /// Marks every warp gate and names where it leads: on the frame beside
        /// a gate near an edge (rotated on the left and right, as retail
        /// does), under the marker otherwise.
        /// </summary>
        private static void DrawLabels(byte[] rgb, int W, int H, MinimapStyleInput input)
        {
            const int F = FRAME;
            using (Bitmap bmp = ToBitmap(rgb, W, H))
            using (Graphics g = Graphics.FromImage(bmp))
            using (FontFamily family = new FontFamily("Arial"))
            using (Font font = new Font(family, 12, FontStyle.Bold, GraphicsUnit.Pixel))
            {
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = TextRenderingHint.AntiAlias;
                List<Rectangle> placed = new List<Rectangle>();
                Dictionary<string, bool> seen = new Dictionary<string, bool>();
                float lineHeight = font.GetHeight(g);

                foreach (MinimapGate gate in input.Gates)
                {
                    int px = (int)Math.Round((gate.X - input.X0) / MinimapZone.M_PER_PX);
                    int py = (int)Math.Round((input.YTop - gate.Y) / MinimapZone.M_PER_PX);
                    if (px < F || px >= W - F || py < F || py >= H - F)
                        continue;

                    Marker(g, px, py);
                    if (string.IsNullOrEmpty(gate.Name))
                        continue;
                    string key = string.Format("{0}|{1}|{2}", gate.Name, px / 48, py / 48);
                    if (seen.ContainsKey(key))
                        continue;
                    seen[key] = true;

                    int[] dists = { py - F, H - F - py, px - F, W - F - px };
                    string[] sides = { "top", "bottom", "left", "right" };
                    int e = 0;
                    for (int i = 1; i < 4; i++)
                        if (dists[i] < dists[e]) e = i;
                    string side = dists[e] < 40 ? sides[e] : "inside";

                    List<string> lines = Wrap(g, gate.Name, font, side == "top" || side == "bottom" ? 96 : side == "inside" ? 120 : 260);
                    float tw = 0;
                    foreach (string l in lines)
                        tw = Math.Max(tw, g.MeasureString(l, font, 1000, StringFormat.GenericTypographic).Width);
                    int bw = (int)Math.Ceiling(tw) + 6, bh = (int)Math.Ceiling(lineHeight * lines.Count) + 4;
                    bool rotated = side == "left" || side == "right";
                    int rw = rotated ? bh : bw, rh = rotated ? bw : bh;

                    Point pos;
                    switch (side)
                    {
                        case "left": pos = new Point(F - rw + 2, py - rh / 2); break;
                        case "right": pos = new Point(W - F - 2, py - rh / 2); break;
                        case "top": pos = new Point(px - rw / 2, F - rh + 2); break;
                        case "bottom": pos = new Point(px - rw / 2, H - F - 2); break;
                        default: pos = new Point(px - rw / 2, py + 6); break;
                    }
                    pos = new Point(Math.Min(Math.Max(pos.X, 4), W - rw - 4), Math.Min(Math.Max(pos.Y, 4), H - rh - 4));
                    for (int step = 0; step < 4; step++)       // step clear of a label already there
                    {
                        Rectangle box = new Rectangle(pos.X, pos.Y, rw, rh);
                        Rectangle hit = placed.Find(delegate(Rectangle b) { return b.IntersectsWith(box); });
                        if (hit.IsEmpty)
                            break;
                        pos = rotated || side == "inside" ? new Point(pos.X, hit.Bottom + 1) : new Point(hit.Right + 2, pos.Y);
                    }
                    placed.Add(new Rectangle(pos.X, pos.Y, rw, rh));

                    using (GraphicsPath path = new GraphicsPath())
                    {
                        for (int i = 0; i < lines.Count; i++)
                        {
                            float lw = g.MeasureString(lines[i], font, 1000, StringFormat.GenericTypographic).Width;
                            path.AddString(lines[i], family, (int)FontStyle.Bold, 12, new PointF((bw - lw) / 2, 2 + i * lineHeight), StringFormat.GenericTypographic);
                        }
                        using (Matrix m = side == "left" ? new Matrix(0, -1, 1, 0, pos.X, pos.Y + bw)
                                        : side == "right" ? new Matrix(0, 1, -1, 0, pos.X + bh, pos.Y)
                                        : new Matrix(1, 0, 0, 1, pos.X, pos.Y))
                            path.Transform(m);
                        using (Pen halo = new Pen(Color.FromArgb(235, 245, 236, 210), 3.0f) { LineJoin = LineJoin.Round })
                            g.DrawPath(halo, path);
                        using (SolidBrush ink = new SolidBrush(Color.FromArgb(20, 14, 8)))
                            g.FillPath(ink, path);
                    }
                }
                g.Flush();
                FromBitmap(bmp, rgb, W, H);
            }
        }

        private static void Marker(Graphics g, int x, int y)
        {
            const int r = 4;
            using (SolidBrush dark = new SolidBrush(Color.FromArgb(60, 10, 5)))
            using (SolidBrush red = new SolidBrush(Color.FromArgb(215, 30, 20)))
            using (SolidBrush light = new SolidBrush(Color.FromArgb(255, 170, 140)))
            {
                g.FillPolygon(dark, new Point[] { new Point(x, y - r - 1), new Point(x + r + 1, y), new Point(x, y + r + 1), new Point(x - r - 1, y) });
                g.FillPolygon(red, new Point[] { new Point(x, y - r), new Point(x + r, y), new Point(x, y + r), new Point(x - r, y) });
                g.FillPolygon(light, new Point[] { new Point(x, y - r + 2), new Point(x + 2, y), new Point(x, y + 1), new Point(x - 2, y) });
            }
        }

        /// <summary>As few lines as fit `width`, two lines balanced ("Valley of / Luxem Tower").</summary>
        private static List<string> Wrap(Graphics g, string text, Font font, float width)
        {
            Func<string, float> len = delegate(string s) { return g.MeasureString(s, font, 1000, StringFormat.GenericTypographic).Width; };
            string[] words = text.Split(new char[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            List<string> lines = new List<string>();
            string cur = "";
            foreach (string w in words)
            {
                string t = cur.Length == 0 ? w : cur + " " + w;
                if (cur.Length > 0 && len(t) > width)
                {
                    lines.Add(cur);
                    cur = w;
                }
                else
                    cur = t;
            }
            if (cur.Length > 0)
                lines.Add(cur);

            if (lines.Count == 2)
            {
                float best = float.MaxValue;
                for (int i = 1; i < words.Length; i++)
                {
                    string a = string.Join(" ", words, 0, i), b = string.Join(" ", words, i, words.Length - i);
                    float m = Math.Max(len(a), len(b));
                    if (m < best)
                    {
                        best = m;
                        lines[0] = a;
                        lines[1] = b;
                    }
                }
            }
            return lines;
        }

        // ------------------------------------------------------------------ bitmaps

        public static Bitmap ToBitmap(byte[] rgb, int W, int H)
        {
            Bitmap bmp = new Bitmap(W, H, PixelFormat.Format24bppRgb);
            BitmapData data = bmp.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
            byte[] row = new byte[data.Stride];
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    int s = (y * W + x) * 3;
                    row[x * 3] = rgb[s + 2];
                    row[x * 3 + 1] = rgb[s + 1];
                    row[x * 3 + 2] = rgb[s];
                }
                Marshal.Copy(row, 0, new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), data.Stride);
            }
            bmp.UnlockBits(data);
            return bmp;
        }

        private static void FromBitmap(Bitmap bmp, byte[] rgb, int W, int H)
        {
            BitmapData data = bmp.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            byte[] row = new byte[data.Stride];
            for (int y = 0; y < H; y++)
            {
                Marshal.Copy(new IntPtr(data.Scan0.ToInt64() + (long)y * data.Stride), row, 0, data.Stride);
                for (int x = 0; x < W; x++)
                {
                    int d = (y * W + x) * 3;
                    rgb[d] = row[x * 3 + 2];
                    rgb[d + 1] = row[x * 3 + 1];
                    rgb[d + 2] = row[x * 3];
                }
            }
            bmp.UnlockBits(data);
        }
    }
}
