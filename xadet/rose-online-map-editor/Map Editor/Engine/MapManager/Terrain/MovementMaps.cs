using System;
using System.Collections.Generic;
using System.IO;
using Map_Editor.Engine.Map;
using Map_Editor.Misc;

namespace Map_Editor.Engine.Terrain
{
    public static class MovementMaps
    {
        public class Block
        {
            public MOV File;
            public Heightmaps.Heightmap Terrain;
            public int X, Y; // HIM filename coordinates.
            public bool Missing, Dirty;
        }

        public static readonly Dictionary<int, Block> Blocks = new Dictionary<int, Block>();
        public static string Error { get; private set; }
        public static bool HasChanges
        {
            get { foreach (Block block in Blocks.Values) if (block.Dirty) return true; return false; }
        }

        public static void Load(Heightmaps terrain)
        {
            Blocks.Clear();
            Error = null;
            bool hasFiles = false;
            try
            {
                foreach (Heightmaps.Heightmap item in terrain.Blocks)
                {
                    if (item == null) continue;
                    string path = Path.ChangeExtension(item.HeightFile.FilePath, ".MOV");
                    string[] xy = Path.GetFileNameWithoutExtension(path).Split('_');
                    Block block = new Block { Terrain = item, X = int.Parse(xy[0]), Y = int.Parse(xy[1]), Missing = !GameData.Exists(path) };
                    block.File = block.Missing ? new MOV { FilePath = path } : new MOV(path);
                    hasFiles |= !block.Missing;
                    Blocks.Add(block.X * 100 + block.Y, block);
                }
                // Match the server: no MOV opens real terrain; partial coverage blocks missing blocks.
                if (hasFiles)
                    foreach (Block block in Blocks.Values)
                        if (block.Missing)
                            for (int y = 0; y < MOV.Size; y++)
                                for (int x = 0; x < MOV.Size; x++) block.File.IsWalkable[y, x] = 1;
            }
            catch (Exception ex)
            {
                Error = "Movement editing unavailable: " + ex.Message;
                Blocks.Clear();
                Output.WriteLine(Output.MessageType.Error, Error);
                Output.WriteException("Movement files", ex);
            }
        }

        // Global cells grow northwards like CZoneFILE::IsMovablePOS (500 cm/cell).
        public static Block Find(int gx, int gy, out int x, out int y)
        {
            x = gx % MOV.Size;
            y = gy % MOV.Size;
            if (gx < 0 || gy < 0 || gx >= 64 * MOV.Size || gy >= 64 * MOV.Size) return null;
            Block block;
            Blocks.TryGetValue((gx / MOV.Size) * 100 + 64 - gy / MOV.Size, out block);
            return block;
        }

        public static int Save(bool generateMissing)
        {
            if (Error != null) throw new InvalidOperationException(Error);
            if (!generateMissing && !HasChanges) return 0;
            foreach (Block block in Blocks.Values)
                if (GameData.IsArchived(block.File.FilePath) || GameData.IsArchived(block.Terrain.HeightFile.FilePath))
                    throw new InvalidOperationException("This map is read from the client's archives (data.idx) and is read-only.");
            int count = 0;
            string backupID = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N");
            foreach (Block block in Blocks.Values)
            {
                // Complete coverage preserves untouched terrain when the first MOV is created.
                if (!block.Dirty && !block.Missing) continue;
                string path = block.File.FilePath;
                string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
                try
                {
                    block.File.Save(temp);
                    if (System.IO.File.Exists(path))
                    {
                        string backupDir = Path.Combine(Path.Combine(Path.GetDirectoryName(path), ".mov-backups"), backupID);
                        Directory.CreateDirectory(backupDir);
                        System.IO.File.Replace(temp, path, Path.Combine(backupDir, Path.GetFileName(path)));
                    }
                    else System.IO.File.Move(temp, path);
                    block.Dirty = block.Missing = false;
                    count++;
                }
                finally
                {
                    block.File.FilePath = path;
                    if (System.IO.File.Exists(temp)) System.IO.File.Delete(temp);
                }
            }
            return count;
        }
    }
}
