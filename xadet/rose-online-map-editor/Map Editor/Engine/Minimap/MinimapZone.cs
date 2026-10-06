using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Map_Editor.Engine.Minimap
{
    /// <summary>
    /// Where a zone's minimap goes and what it covers, and writing it there.
    ///
    /// The client places a minimap at 64 px per 160 m chunk (2.5 m a pixel)
    /// with a one-chunk margin on every side, which the parchment frame fills;
    /// LIST_ZONE game column 8 names the DDS and columns 9/10 the chunk folder
    /// x_y of the top-left chunk inside the margin (CMinimapDLG::
    /// CalculateDisplayPos). A zone that has a minimap keeps its texture's
    /// extent, so a new one lines up with the old pixel for pixel; a zone
    /// without one gets its chunks' bounding box.
    /// </summary>
    public class MinimapZone
    {
        public const int PX_PER_CHUNK = 64;
        public const float M_PER_PX = 2.5f;
        public const string BACKUP_FOLDER = "MinimapBackups";

        // LIST_ZONE game columns (the editor's Cells index is one more)
        private const int COL_ZON = 1, COL_MINIMAP = 8, COL_MM_X = 9, COL_MM_Y = 10;
        private const string ZONE_STB = "3DDATA\\STB\\LIST_ZONE.STB";

        public int ID { get; private set; }
        public string Folder { get; private set; }
        public string ZoneDirectory { get; private set; }

        /// <summary>The DDS LIST_ZONE names, or null for NOMAP / blank.</summary>
        public string MinimapPath { get; private set; }

        public int StartX { get; private set; }
        public int StartY { get; private set; }
        public int Width { get; private set; }
        public int Height { get; private set; }

        /// <summary>True when the extent came from an existing minimap.</summary>
        public bool HasMinimap { get; private set; }

        /// <summary>Editor metres of the texture's left edge and top edge.</summary>
        public float X0 { get { return 160.0f * (StartX - 1); } }
        public float YTop { get { return 160.0f * (66 - StartY); } }

        /// <summary>Where Install writes: the named DDS, else MINIMAP.DDS in the zone folder.</summary>
        public string TargetPath
        {
            get { return MinimapPath ?? Path.Combine(ZoneDirectory, "MINIMAP.DDS"); }
        }

        public string BackupDirectory
        {
            get { return Path.Combine(BACKUP_FOLDER, Folder); }
        }

        private static string Cell(int row, int col)
        {
            List<string> cells = FileManager.STBs["LIST_ZONE"].Cells[row];
            return col + 1 < cells.Count ? cells[col + 1].Trim() : "";
        }

        public static MinimapZone ForZone(int id)
        {
            MinimapZone z = new MinimapZone();
            z.ID = id;
            z.ZoneDirectory = Path.GetDirectoryName(Cell(id, COL_ZON));
            z.Folder = Path.GetFileName(z.ZoneDirectory).ToUpperInvariant();

            string mm = Cell(id, COL_MINIMAP);
            z.MinimapPath = mm.Length == 0 || string.Compare(mm, "NOMAP", true) == 0 ? null : mm;

            int sx, sy, w, h;
            byte[] dds = z.MinimapPath != null && GameData.Exists(z.MinimapPath) ? GameData.ReadAllBytes(z.MinimapPath) : null;
            if (dds != null && int.TryParse(Cell(id, COL_MM_X), out sx) && int.TryParse(Cell(id, COL_MM_Y), out sy) && sx > 0
                && DdsCodec.ReadSize(dds, out w, out h))
            {
                z.StartX = sx;
                z.StartY = sy;
                z.Width = w;
                z.Height = h;
                z.HasMinimap = true;
                return z;
            }

            int minX = int.MaxValue, maxX = int.MinValue, minY = int.MaxValue, maxY = int.MinValue;
            foreach (string file in GameData.GetFiles(z.ZoneDirectory, "*.HIM"))
            {
                Match m = Regex.Match(Path.GetFileName(file), @"^(\d+)_(\d+)\.him$", RegexOptions.IgnoreCase);
                if (!m.Success)
                    continue;

                int x = int.Parse(m.Groups[1].Value), y = int.Parse(m.Groups[2].Value);
                minX = Math.Min(minX, x); maxX = Math.Max(maxX, x);
                minY = Math.Min(minY, y); maxY = Math.Max(maxY, y);
            }
            if (minX == int.MaxValue)
                throw new InvalidDataException("the zone folder has no heightmaps");

            z.StartX = minX;
            z.StartY = minY;
            z.Width = (maxX - minX + 3) * PX_PER_CHUNK;
            z.Height = (maxY - minY + 3) * PX_PER_CHUNK;
            return z;
        }

        /// <summary>The current minimap's pixels, or null.</summary>
        public byte[] ReadCurrent()
        {
            if (!HasMinimap)
                return null;

            int w, h;
            byte[] rgb = DdsCodec.Decode(GameData.ReadAllBytes(MinimapPath), out w, out h);
            return w == Width && h == Height ? rgb : null;
        }

        /// <summary>The minimap from before the first Install, else the current one; null if none.</summary>
        public static byte[] ReadOriginal(string ddsPath, out int width, out int height)
        {
            width = height = 0;
            string folder = Path.GetFileName(Path.GetDirectoryName(ddsPath)).ToUpperInvariant();
            string backup = Path.Combine(Path.Combine(BACKUP_FOLDER, folder), Path.GetFileName(ddsPath));
            byte[] dds = File.Exists(backup) ? File.ReadAllBytes(backup) : GameData.Exists(ddsPath) ? GameData.ReadAllBytes(ddsPath) : null;
            return dds == null ? null : DdsCodec.Decode(dds, out width, out height);
        }

        public bool IsInstalled
        {
            get { return File.Exists(Path.Combine(BackupDirectory, "installed.txt")); }
        }

        /// <summary>Why Install cannot write here, or null.</summary>
        public string InstallProblem()
        {
            if (GameData.IsArchived(TargetPath) && !File.Exists(TargetPath))
                return "the minimap is inside the game's archive (data.idx); the editor never writes over packed files";
            if (MinimapPath == null && GameData.IsArchived(ZONE_STB) && !File.Exists(ZONE_STB))
                return "LIST_ZONE.STB is inside the game's archive (data.idx); the editor never writes over packed files";
            return null;
        }

        /// <summary>
        /// Writes the picture as the zone's minimap DDS. The first Install keeps
        /// the previous file (and, for a zone without a minimap, the LIST_ZONE
        /// cells it fills) under MinimapBackups\FOLDER\ for Restore.
        /// </summary>
        public void Install(byte[] rgb)
        {
            string problem = InstallProblem();
            if (problem != null)
                throw new InvalidOperationException(problem);

            Directory.CreateDirectory(BackupDirectory);
            string marker = Path.Combine(BackupDirectory, "installed.txt");
            if (!File.Exists(marker))
            {
                List<string> lines = new List<string>();
                lines.Add("target " + TargetPath);
                if (File.Exists(TargetPath))
                    File.Copy(TargetPath, Path.Combine(BackupDirectory, Path.GetFileName(TargetPath)), true);
                else
                    lines.Add("new");
                if (MinimapPath == null)
                {
                    lines.Add(string.Format("row {0}", ID));
                    for (int c = COL_MINIMAP; c <= COL_MM_Y; c++)
                        lines.Add(string.Format("cell {0} {1}", c, Cell(ID, c)));
                }
                File.WriteAllLines(marker, lines.ToArray());
            }

            File.WriteAllBytes(TargetPath, DdsCodec.EncodeDxt5(rgb, Width, Height));

            if (MinimapPath == null)
            {
                Dictionary<int, string> cells = new Dictionary<int, string>();
                cells[COL_MINIMAP] = TargetPath;
                cells[COL_MM_X] = StartX.ToString();
                cells[COL_MM_Y] = StartY.ToString();
                SetCells(ID, cells);
                MinimapPath = TargetPath;
            }
            HasMinimap = true;
        }

        /// <summary>Puts back what the first Install replaced.</summary>
        public void Restore()
        {
            string marker = Path.Combine(BackupDirectory, "installed.txt");
            if (!File.Exists(marker))
                throw new InvalidOperationException("nothing was installed for this zone");

            string target = null;
            bool isNew = false;
            int row = -1;
            Dictionary<int, string> cells = new Dictionary<int, string>();
            foreach (string line in File.ReadAllLines(marker))
            {
                if (line.StartsWith("target "))
                    target = line.Substring(7);
                else if (line == "new")
                    isNew = true;
                else if (line.StartsWith("row "))
                    row = int.Parse(line.Substring(4));
                else if (line.StartsWith("cell "))
                {
                    string[] w = line.Split(new char[] { ' ' }, 3);
                    cells[int.Parse(w[1])] = w.Length > 2 ? w[2] : "";
                }
            }

            if (isNew)
                File.Delete(target);
            else
                File.Copy(Path.Combine(BackupDirectory, Path.GetFileName(target)), target, true);

            if (row >= 0)
            {
                SetCells(row, cells);
                if (row == ID)
                {
                    MinimapPath = null;
                    HasMinimap = false;
                }
            }

            Directory.Delete(BackupDirectory, true);
        }

        /// <summary>
        /// Rewrites LIST_ZONE cells byte for byte: the editor's own STB.Save
        /// round-trips every cell through EUC-KR, which would mangle any
        /// non-Korean byte elsewhere in the table. Also updates the loaded copy.
        /// </summary>
        private static void SetCells(int row, Dictionary<int, string> values)
        {
            byte[] raw = File.ReadAllBytes(ZONE_STB);
            int dataOffset = BitConverter.ToInt32(raw, 4);
            int rawRows = BitConverter.ToInt32(raw, 8), rawCols = BitConverter.ToInt32(raw, 12);
            int rows = rawRows - 1, cols = rawCols - 1;

            List<byte[]> cells = new List<byte[]>(rows * cols);
            int o = dataOffset;
            for (int i = 0; i < rows * cols; i++)
            {
                int n = BitConverter.ToUInt16(raw, o);
                byte[] cell = new byte[n];
                Buffer.BlockCopy(raw, o + 2, cell, 0, n);
                cells.Add(cell);
                o += 2 + n;
            }

            foreach (KeyValuePair<int, string> kv in values)
            {
                cells[row * cols + kv.Key] = Encoding.ASCII.GetBytes(kv.Value);
                FileManager.STBs["LIST_ZONE"].Cells[row][kv.Key + 1] = kv.Value;
            }

            MemoryStream ms = new MemoryStream();
            ms.Write(raw, 0, dataOffset);
            foreach (byte[] cell in cells)
            {
                ms.Write(BitConverter.GetBytes((ushort)cell.Length), 0, 2);
                ms.Write(cell, 0, cell.Length);
            }
            ms.Write(raw, o, raw.Length - o);

            string temp = ZONE_STB + ".tmp";
            File.WriteAllBytes(temp, ms.ToArray());
            File.Copy(temp, ZONE_STB, true);
            File.Delete(temp);
        }
    }
}
