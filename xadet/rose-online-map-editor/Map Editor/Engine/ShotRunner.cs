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
    ///
    /// Writes out\shots.txt (one line per saved view, then "done") so the
    /// caller can tell a finished run from a crash. Overlays drawn after the
    /// world (tool gizmos, tooltips, the preview panel) are left out.
    /// </summary>
    public static class ShotRunner
    {
        private enum State { Waiting, Loading, Settling, Aiming, Done }

        private class View
        {
            public string Name;
            public Vector3 Eye, Target;
        }

        /// <summary>Frames rendered at a view before it is saved.</summary>
        private const int AIM_FRAMES = 12;

        /// <summary>The whole run gives up after this long (a failed load shows a message box).</summary>
        private static readonly TimeSpan TIMEOUT = TimeSpan.FromMinutes(5);

        public static bool Active { get; private set; }

        private static int zone;
        private static string outDir;
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
                        default:
                            throw new Exception("shots job: unknown directive " + w[0]);
                    }
                }

                if (zone <= 0 || outDir == null || views.Count == 0)
                    throw new Exception("shots job needs zone, out and at least one view");

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
                        Aim(0);
                    break;
                case State.Aiming:
                    if (++frames == AIM_FRAMES)
                        captureNow = true;
                    break;
            }
        }

        private static void Aim(int index)
        {
            viewIndex = index;
            CameraManager.PerspectiveCamera.SetLookAt(views[index].Eye, views[index].Target);
            state = State.Aiming;
            frames = 0;
        }

        /// <summary>
        /// Called right after the world is drawn: saves the frame when a view
        /// has had its frames to settle.
        /// </summary>
        public static void AfterWorldDraw(GraphicsDevice device)
        {
            if (!captureNow)
                return;

            captureNow = false;
            View view = views[viewIndex];
            string path = Path.Combine(outDir, view.Name + ".png");

            PresentationParameters pp = device.PresentationParameters;
            using (ResolveTexture2D texture = new ResolveTexture2D(device, pp.BackBufferWidth, pp.BackBufferHeight, 1, pp.BackBufferFormat))
            {
                device.ResolveBackBuffer(texture);
                texture.Save(path, ImageFileFormat.Png);
            }

            saved.Add(string.Format("{0} {1}x{2}", view.Name, pp.BackBufferWidth, pp.BackBufferHeight));
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
