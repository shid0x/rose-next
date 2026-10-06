using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Map_Editor.Engine
{
    /// <summary>
    /// Unattended screenshots: <c>"Map Editor.exe" --shots job.txt</c> loads a
    /// zone, renders it from each view in the job and saves the back buffer
    /// as a PNG per view, then exits. Used by the map generator
    /// (scripts/mapgen-zone.py shots) so a generated map can be looked at
    /// without anyone driving the editor.
    ///
    /// Job file, one directive per line ('#' starts a comment):
    ///   zone 12
    ///   out C:\path\to\folder
    ///   hide Collision SpawnPoints WarpGates ...   ("Draw" settings to turn off)
    ///   settle 60                                  (frames after the load)
    ///   view name ex ey ez tx ty tz                (eye and target, editor metres)
    ///   ortho name x0 y0 x1 y1 width height        (straight down on a rectangle,
    ///                                               editor metres, north up, into
    ///                                               a width x height image)
    ///   water r g b                                (draw water opaque in this
    ///                                               colour, 0-255: a key for
    ///                                               minimap shots)
    ///   minimap C:\path\minimap.png               (Tools > Make minimap, unattended:
    ///                                               the zone's minimap with the
    ///                                               default options, before the views)
    ///
    /// A view is the render panel's size; an ortho view is rendered off screen
    /// at its own size, so it does not depend on the window (scripts/
    /// make-minimap.py tiles a whole map with them).
    ///
    /// Writes out\shots.txt (one line per saved view, then "done") so the
    /// caller can tell a finished run from a crash. Overlays drawn after the
    /// world (tool gizmos, tooltips, the preview panel) are left out.
    /// </summary>
    public static class ShotRunner
    {
        private enum State { Waiting, Loading, Settling, Minimap, Aiming, Done }

        private class View
        {
            public string Name;
            public Vector3 Eye, Target;

            /// <summary>Ortho views: the rectangle (x0, y0, x1, y1) and the image size.</summary>
            public bool Ortho;
            public Vector4 Rect;
            public int Width, Height;
        }

        /// <summary>The water colour of a minimap job, if it set one.</summary>
        public static Vector4? WaterKey { get; private set; }

        private static RenderTarget2D target;
        private static DepthStencilBuffer targetDepth;
        private static DepthStencilBuffer savedDepth;

        /// <summary>Frames rendered at a view before it is saved.</summary>
        private const int AIM_FRAMES = 12;

        /// <summary>The whole run gives up after this long (a failed load shows a message box).</summary>
        private static readonly TimeSpan TIMEOUT = TimeSpan.FromMinutes(5);

        public static bool Active { get; private set; }

        private static int zone;
        private static string outDir;
        private static string minimapOut;
        private static int settleFrames = 60;
        private static readonly List<string> hide = new List<string>();
        private static readonly List<View> views = new List<View>();
        private static readonly List<string> saved = new List<string>();

        private static State state = State.Waiting;
        private static int frames;
        private static int viewIndex;
        private static bool captureNow;
        private static DateTime started;

        /// <summary>
        /// Reads the job named after --shots on the command line, if any.
        /// </summary>
        public static void Parse(string[] args)
        {
            for (int i = 0; i < args.Length - 1; i++)
            {
                if (args[i] != "--shots")
                    continue;

                string[] lines = File.ReadAllLines(args[i + 1]);
                foreach (string raw in lines)
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line.StartsWith("#"))
                        continue;

                    string[] w = line.Split(new char[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                    switch (w[0])
                    {
                        case "zone":
                            zone = int.Parse(w[1], CultureInfo.InvariantCulture);
                            break;
                        case "out":
                            outDir = line.Substring(3).Trim();
                            break;
                        case "settle":
                            settleFrames = int.Parse(w[1], CultureInfo.InvariantCulture);
                            break;
                        case "hide":
                            for (int k = 1; k < w.Length; k++)
                                hide.Add(w[k]);
                            break;
                        case "view":
                            views.Add(new View
                            {
                                Name = w[1],
                                Eye = new Vector3(F(w[2]), F(w[3]), F(w[4])),
                                Target = new Vector3(F(w[5]), F(w[6]), F(w[7]))
                            });
                            break;
                        case "ortho":
                            views.Add(new View
                            {
                                Name = w[1],
                                Ortho = true,
                                Rect = new Vector4(F(w[2]), F(w[3]), F(w[4]), F(w[5])),
                                Width = int.Parse(w[6], CultureInfo.InvariantCulture),
                                Height = int.Parse(w[7], CultureInfo.InvariantCulture)
                            });
                            break;
                        case "minimap":
                            minimapOut = line.Substring(7).Trim();
                            break;
                        case "water":
                            WaterKey = new Vector4(F(w[1]) / 255.0f, F(w[2]) / 255.0f, F(w[3]) / 255.0f, 1.0f);
                            break;
                        default:
                            throw new Exception("shots job: unknown directive " + w[0]);
                    }
                }

                if (zone <= 0 || outDir == null || (views.Count == 0 && minimapOut == null))
                    throw new Exception("shots job needs zone, out and at least one view or minimap");

                Directory.CreateDirectory(outDir);
                File.Delete(Path.Combine(outDir, "shots.txt"));
                Active = true;
                started = DateTime.Now;
                return;
            }
        }

        private static float F(string s)
        {
            return float.Parse(s, CultureInfo.InvariantCulture);
        }

        /// <summary>
        /// Called once the editor has finished starting: hides the editing
        /// helpers (in memory only, the config file is not saved) and loads
        /// the zone.
        /// </summary>
        public static void Begin()
        {
            foreach (string layer in hide)
                ConfigurationManager.SetValue("Draw", layer, false);

            Output.WriteLine(Output.MessageType.Event, string.Format("Shots: zone {0}, {1} view(s) -> {2}", zone, views.Count, outDir));
            MapManager.Load(zone);
            state = State.Loading;
        }

        private static bool Loading
        {
            get
            {
                return MapManager.Sky.Loading || MapManager.Heightmaps.Loading || MapManager.Decoration.Loading
                    || MapManager.Construction.Loading || MapManager.Water.Loading || MapManager.Animation.Loading;
            }
        }

        /// <summary>
        /// Advances the run; called every engine update.
        /// </summary>
        public static void Update()
        {
            if (!Active)
                return;

            if (DateTime.Now - started > TIMEOUT)
                Finish("timeout after " + TIMEOUT.TotalMinutes + " minutes in state " + state);

            switch (state)
            {
                case State.Loading:
                    if (!Loading)
                    {
                        state = State.Settling;
                        frames = 0;
                    }
                    break;
                case State.Settling:
                    if (++frames >= settleFrames)
                    {
                        if (minimapOut != null)
                            StartMinimap();
                        else
                            Aim(0);
                    }
                    break;
                case State.Aiming:
                    if (++frames == AIM_FRAMES)
                        captureNow = true;
                    break;
            }
        }

        /// <summary>Runs Tools > Make minimap's capture and styling with its default options.</summary>
        private static void StartMinimap()
        {
            state = State.Minimap;
            Minimap.MinimapZone mz = Minimap.MinimapZone.ForZone(zone);
            Minimap.MinimapCapture.Start(mz, 4,
                delegate(Minimap.MinimapRender render)
                {
                    Minimap.MinimapStyleInput input = Minimap.MinimapInputs.Gather(mz, render, Minimap.MinimapInputs.Original(mz), true, true, true);
                    Minimap.MinimapResult result = Minimap.MinimapStyle.Make(input);
                    using (System.Drawing.Bitmap bmp = Minimap.MinimapStyle.ToBitmap(result.Rgb, result.Width, result.Height))
                        bmp.Save(minimapOut, System.Drawing.Imaging.ImageFormat.Png);
                    saved.Add(string.Format(CultureInfo.InvariantCulture, "minimap {0}x{1} difference {2:0.0}", result.Width, result.Height, result.Difference));
                    Output.WriteLine(Output.MessageType.Normal, "Shots: saved " + minimapOut);
                    if (views.Count > 0)
                        Aim(0);
                    else
                        Finish("done");
                },
                delegate(string error)
                {
                    Finish("minimap failed: " + error);
                });
        }

        private static void Aim(int index)
        {
            viewIndex = index;
            View view = views[index];
            if (view.Ortho)
            {
                CameraManager.OrthographicCamera.SetTopDown(view.Rect.X, view.Rect.Y, view.Rect.Z, view.Rect.W);
                CameraManager.SetCameraType(CameraManager.CameraType.Orthographic);
            }
            else
            {
                CameraManager.SetCameraType(CameraManager.CameraType.Perspective);
                CameraManager.PerspectiveCamera.SetLookAt(view.Eye, view.Target);
            }
            state = State.Aiming;
            frames = 0;
        }

        /// <summary>
        /// Called right before the world is drawn: an ortho view draws into
        /// its own render target, sized for its image.
        /// </summary>
        public static void BeforeWorldDraw(GraphicsDevice device)
        {
            if (state != State.Aiming || !views[viewIndex].Ortho)
                return;

            View view = views[viewIndex];
            if (target == null || target.Width != view.Width || target.Height != view.Height)
            {
                ReleaseTarget();
                target = new RenderTarget2D(device, view.Width, view.Height, 1, SurfaceFormat.Color, MultiSampleType.None, 0);
                targetDepth = new DepthStencilBuffer(device, view.Width, view.Height, DepthFormat.Depth24Stencil8, MultiSampleType.None, 0);
            }

            savedDepth = device.DepthStencilBuffer;
            device.SetRenderTarget(0, target);
            device.DepthStencilBuffer = targetDepth;
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

        /// <summary>
        /// Called right after the world is drawn: saves the frame when a view
        /// has had its frames to settle.
        /// </summary>
        public static void AfterWorldDraw(GraphicsDevice device)
        {
            bool offscreen = savedDepth != null;
            if (offscreen)
            {
                device.SetRenderTarget(0, null);
                device.DepthStencilBuffer = savedDepth;
                savedDepth = null;
            }

            if (!captureNow)
                return;

            captureNow = false;
            View view = views[viewIndex];
            string path = Path.Combine(outDir, view.Name + ".png");

            int width, height;
            if (offscreen)
            {
                Texture2D texture = target.GetTexture();
                texture.Save(path, ImageFileFormat.Png);
                width = target.Width;
                height = target.Height;
            }
            else
            {
                PresentationParameters pp = device.PresentationParameters;
                using (ResolveTexture2D texture = new ResolveTexture2D(device, pp.BackBufferWidth, pp.BackBufferHeight, 1, pp.BackBufferFormat))
                {
                    device.ResolveBackBuffer(texture);
                    texture.Save(path, ImageFileFormat.Png);
                }
                width = pp.BackBufferWidth;
                height = pp.BackBufferHeight;
            }

            saved.Add(string.Format("{0} {1}x{2}", view.Name, width, height));
            Output.WriteLine(Output.MessageType.Normal, "Shots: saved " + path);

            if (viewIndex + 1 < views.Count)
                Aim(viewIndex + 1);
            else
                Finish("done");
        }

        private static void Finish(string how)
        {
            state = State.Done;
            List<string> lines = new List<string>(saved);
            lines.Add(how);
            File.WriteAllLines(Path.Combine(outDir, "shots.txt"), lines.ToArray());
            Output.WriteLine(Output.MessageType.Event, "Shots: " + how);
            Environment.Exit(how == "done" ? 0 : 1);
        }
    }
}
