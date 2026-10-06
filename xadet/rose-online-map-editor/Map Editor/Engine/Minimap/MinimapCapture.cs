using System;
using System.Collections.Generic;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Map_Editor.Engine.Minimap
{
    /// <summary>
    /// What the capture saw, at the minimap's own resolution: the land's
    /// colour, and how much of each pixel was land and water (the rest is
    /// void: no terrain).
    /// </summary>
    public class MinimapRender
    {
        public int Width, Height;
        public float[] Land;      // RGB, 0-255, valid where LandCover > 0
        public float[] LandCover;
        public float[] WaterCover;
    }

    /// <summary>
    /// Renders the open map from straight above into off-screen tiles, one
    /// tile every few frames, inside the editor's own draw loop -- unsaved
    /// edits included. Each tile is drawn `scale` times larger than the
    /// minimap and averaged down (retail's detail level is a box filter from
    /// 4x). Editing helpers, sky, NPCs and monsters are hidden for the run and
    /// water is drawn in a key colour, then everything is put back.
    /// </summary>
    public static class MinimapCapture
    {
        private const int TILE_PX = 2048;
        private const int FRAMES_PER_TILE = 3;
        private static readonly string[] HIDE = { "Collision", "SpawnPoints", "WarpGates", "Sounds", "Effects",
                                                  "EventTriggers", "GridOutline", "GridNumbers", "Sky", "NPCs", "Monsters" };

        private class Tile
        {
            public int Left, Top, Width, Height;
            public float X0, Y0, X1, Y1;
        }

        public static bool Active { get; private set; }

        /// <summary>Water colour while capturing (Water.Draw keys on it).</summary>
        public static Vector4? WaterKey
        {
            get { return Active ? (Vector4?)new Vector4(1.0f, 0.0f, 1.0f, 1.0f) : null; }
        }

        private static List<Tile> tiles;
        private static int tileIndex, frames, scale;
        private static MinimapRender result;
        private static float[] landSum;
        private static int[] landCount, waterCount;
        private static Dictionary<string, bool> savedDraw;
        private static RenderTarget2D target;
        private static DepthStencilBuffer targetDepth, savedDepth;
        private static Action<MinimapRender> onDone;
        private static Action<string> onError;

        public static void Start(MinimapZone zone, int renderScale, Action<MinimapRender> done, Action<string> failed)
        {
            if (Active)
                throw new InvalidOperationException("a minimap capture is already running");

            scale = renderScale;
            onDone = done;
            onError = failed;
            result = new MinimapRender { Width = zone.Width, Height = zone.Height };
            int n = zone.Width * zone.Height;
            landSum = new float[n * 3];
            landCount = new int[n];
            waterCount = new int[n];

            int W = zone.Width * scale, H = zone.Height * scale;
            float mpp = MinimapZone.M_PER_PX / scale;
            tiles = new List<Tile>();
            for (int top = 0; top < H; top += TILE_PX)
            {
                for (int left = 0; left < W; left += TILE_PX)
                {
                    int tw = Math.Min(TILE_PX, W - left), th = Math.Min(TILE_PX, H - top);
                    tiles.Add(new Tile
                    {
                        Left = left, Top = top, Width = tw, Height = th,
                        X0 = zone.X0 + left * mpp, X1 = zone.X0 + (left + tw) * mpp,
                        Y0 = zone.YTop - (top + th) * mpp, Y1 = zone.YTop - top * mpp
                    });
                }
            }

            savedDraw = new Dictionary<string, bool>();
            foreach (string layer in HIDE)
            {
                savedDraw[layer] = ConfigurationManager.GetValue<bool>("Draw", layer);
                ConfigurationManager.SetValue("Draw", layer, false);
            }

            tileIndex = 0;
            frames = 0;
            Active = true;
            Output.WriteLine(Output.MessageType.Event, string.Format("Minimap: {0}x{1} at {2}x, {3} tile(s)", zone.Width, zone.Height, scale, tiles.Count));
        }

        /// <summary>Called right before the world is drawn.</summary>
        public static void BeforeWorldDraw(GraphicsDevice device)
        {
            if (!Active)
                return;

            try
            {
                Tile t = tiles[tileIndex];
                CameraManager.OrthographicCamera.SetTopDown(t.X0, t.Y0, t.X1, t.Y1);
                CameraManager.SetCameraType(CameraManager.CameraType.Orthographic);

                if (target == null || target.Width != t.Width || target.Height != t.Height)
                {
                    ReleaseTarget();
                    target = new RenderTarget2D(device, t.Width, t.Height, 1, SurfaceFormat.Color, MultiSampleType.None, 0);
                    targetDepth = new DepthStencilBuffer(device, t.Width, t.Height, DepthFormat.Depth24Stencil8, MultiSampleType.None, 0);
                }

                savedDepth = device.DepthStencilBuffer;
                device.SetRenderTarget(0, target);
                device.DepthStencilBuffer = targetDepth;
            }
            catch (Exception e)
            {
                Fail(e);
            }
        }

        /// <summary>Called right after the world is drawn.</summary>
        public static void AfterWorldDraw(GraphicsDevice device)
        {
            if (!Active || savedDepth == null)
                return;

            device.SetRenderTarget(0, null);
            device.DepthStencilBuffer = savedDepth;
            savedDepth = null;

            if (++frames < FRAMES_PER_TILE)
                return;

            try
            {
                Accumulate(tiles[tileIndex]);
                frames = 0;
                if (++tileIndex == tiles.Count)
                    Finish();
            }
            catch (Exception e)
            {
                Fail(e);
            }
        }

        private static void Accumulate(Tile t)
        {
            Color[] data = new Color[t.Width * t.Height];
            target.GetTexture().GetData(data);
            int W = result.Width;
            for (int y = 0; y < t.Height; y++)
            {
                int row = ((t.Top + y) / scale) * W;
                for (int x = 0; x < t.Width; x++)
                {
                    Color c = data[y * t.Width + x];
                    int p = row + (t.Left + x) / scale;
                    if (c.R == 255 && c.G == 0 && c.B == 255)
                        waterCount[p]++;
                    else if (c.R == 127 && c.G == 127 && c.B == 127)
                        continue;                        // the editor's clear colour: no terrain
                    else
                    {
                        landCount[p]++;
                        landSum[p * 3] += c.R;
                        landSum[p * 3 + 1] += c.G;
                        landSum[p * 3 + 2] += c.B;
                    }
                }
            }
        }

        private static void Finish()
        {
            int n = result.Width * result.Height;
            float per = 1.0f / (scale * scale);
            result.Land = new float[n * 3];
            result.LandCover = new float[n];
            result.WaterCover = new float[n];
            for (int i = 0; i < n; i++)
            {
                result.LandCover[i] = landCount[i] * per;
                result.WaterCover[i] = waterCount[i] * per;
                if (landCount[i] > 0)
                    for (int c = 0; c < 3; c++)
                        result.Land[i * 3 + c] = landSum[i * 3 + c] / landCount[i];
            }

            MinimapRender done = result;
            Action<MinimapRender> callback = onDone;
            Stop();
            Output.WriteLine(Output.MessageType.Event, "Minimap: render finished");
            callback(done);
        }

        private static void Fail(Exception e)
        {
            Output.WriteException("Minimap capture failed", e);
            Action<string> callback = onError;
            Stop();
            if (callback != null)
                callback(e.Message);
        }

        private static void Stop()
        {
            Active = false;
            ReleaseTarget();
            CameraManager.SetCameraType(CameraManager.CameraType.Perspective);
            if (savedDraw != null)
                foreach (KeyValuePair<string, bool> kv in savedDraw)
                    ConfigurationManager.SetValue("Draw", kv.Key, kv.Value);
            savedDraw = null;
            landSum = null;
            landCount = waterCount = null;
            tiles = null;
            result = null;
            onDone = null;
            onError = null;
        }

        private static void ReleaseTarget()
        {
            if (target != null)
                target.Dispose();
            if (targetDepth != null)
                targetDepth.Dispose();
            target = null;
            targetDepth = null;
        }
    }
}
