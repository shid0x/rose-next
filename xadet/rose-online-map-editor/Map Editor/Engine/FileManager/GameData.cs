using System;
using System.Collections.Generic;
using System.IO;
using Map_Editor.Misc;
using Microsoft.Xna.Framework.Graphics;

namespace Map_Editor.Engine
{
    /// <summary>
    /// Where the editor reads game files from: loose files under the working directory
    /// first, then the client's packed archives (data.idx + .vfs) if it has them.
    /// </summary>
    /// <remarks>
    /// Loose files win, as in a client's own lookup, so a folder of edited files still
    /// overrides the archives. Archived files are read-only: a map that comes from an
    /// archive cannot be saved (<see cref="IsArchived"/>), because writing its files
    /// loose would quietly fork the client's data.
    /// </remarks>
    public static class GameData
    {
        private static VfsArchive archive;
        private static string root;

        /// <summary>
        /// Gets whether any archive is mounted.
        /// </summary>
        public static bool HasArchive
        {
            get { return archive != null; }
        }

        /// <summary>
        /// Mounts the data.idx in a folder, if there is one. Safe to call again.
        /// </summary>
        /// <param name="folder">The folder (the editor's working directory).</param>
        public static void Mount(string folder)
        {
            string fullFolder = Path.GetFullPath(folder);

            if (root != null && string.Compare(root, fullFolder, true) == 0)
                return;

            root = fullFolder;
            archive = null;

            string indexPath = Path.Combine(fullFolder, "data.idx");

            if (!File.Exists(indexPath))
                return;

            Output.WriteLine(Output.MessageType.Event, "Mounting client archives");

            try
            {
                archive = new VfsArchive(indexPath);
                Output.WriteLine(Output.MessageType.Normal, "- " + archive.Summary);
            }
            catch (Exception ex)
            {
                Output.WriteException(string.Format("Could not read {0}; using loose files only", indexPath), ex);
                archive = null;
            }
        }

        /// <summary>
        /// Turns a path into an archive key: relative to the root, upper case, single
        /// backslashes. Data paths mix '/' and '\', doubled separators and case freely.
        /// </summary>
        public static string Normalize(string path)
        {
            if (path == null)
                return "";

            string key = path.Trim().Replace('/', '\\');

            if (root != null && key.Length > root.Length && key.StartsWith(root, StringComparison.OrdinalIgnoreCase) && key[root.Length] == '\\')
                key = key.Substring(root.Length + 1);

            while (key.Contains("\\\\"))
                key = key.Replace("\\\\", "\\");

            while (key.StartsWith(".\\"))
                key = key.Substring(2);

            return key.TrimStart('\\').ToUpperInvariant();
        }

        private static VfsArchive.Entry Find(string path)
        {
            if (archive == null || path == null)
                return null;

            VfsArchive.Entry entry;
            return archive.Files.TryGetValue(Normalize(path), out entry) ? entry : null;
        }

        /// <summary>
        /// Whether a file exists loose or in the archives.
        /// </summary>
        public static bool Exists(string path)
        {
            if (string.IsNullOrEmpty(path))
                return false;

            return File.Exists(path) || Find(path) != null;
        }

        /// <summary>
        /// Whether a file exists only in the archives (so it cannot be written back).
        /// </summary>
        public static bool IsArchived(string path)
        {
            if (string.IsNullOrEmpty(path))
                return false;

            return !File.Exists(path) && Find(path) != null;
        }

        /// <summary>
        /// Opens a file for reading; an archived file is decoded into memory.
        /// </summary>
        public static Stream OpenRead(string path)
        {
            if (File.Exists(path))
                return new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);

            VfsArchive.Entry entry = Find(path);

            if (entry == null)
                return File.OpenRead(path);     // throws the usual FileNotFoundException

            return new MemoryStream(archive.Read(entry), false);
        }

        /// <summary>
        /// Reads a whole file.
        /// </summary>
        public static byte[] ReadAllBytes(string path)
        {
            if (File.Exists(path))
                return File.ReadAllBytes(path);

            VfsArchive.Entry entry = Find(path);

            if (entry == null)
                return File.ReadAllBytes(path);

            return archive.Read(entry);
        }

        /// <summary>
        /// Loads a texture loose or from the archives.
        /// </summary>
        public static Texture2D LoadTexture(GraphicsDevice device, string path)
        {
            if (File.Exists(path))
                return Texture2D.FromFile(device, path);

            VfsArchive.Entry entry = Find(path);

            if (entry == null)
                return Texture2D.FromFile(device, path);

            using (MemoryStream stream = new MemoryStream(archive.Read(entry), false))
                return Texture2D.FromFile(device, stream);
        }

        /// <summary>
        /// Whether a folder exists loose or in the archives.
        /// </summary>
        public static bool DirectoryExists(string folder)
        {
            if (Directory.Exists(folder))
                return true;

            return archive != null && archive.Folders.ContainsKey(Normalize(folder));
        }

        /// <summary>
        /// Lists a folder's files matching "*.EXT", loose and archived, without duplicates.
        /// Archived names are returned under the folder as given.
        /// </summary>
        public static string[] GetFiles(string folder, string pattern)
        {
            List<string> files = new List<string>();
            Dictionary<string, bool> seen = new Dictionary<string, bool>();

            if (Directory.Exists(folder))
            {
                foreach (string file in Directory.GetFiles(folder, pattern))
                {
                    files.Add(file);
                    seen[Normalize(file)] = true;
                }
            }

            List<string> names;

            if (archive != null && archive.Folders.TryGetValue(Normalize(folder), out names))
            {
                string extension = pattern.StartsWith("*") ? pattern.Substring(1).ToUpperInvariant() : null;

                foreach (string name in names)
                {
                    if (extension != null ? !name.EndsWith(extension) : string.Compare(name, pattern, true) != 0)
                        continue;

                    string file = folder.TrimEnd('\\', '/') + "\\" + name;

                    if (seen.ContainsKey(Normalize(file)))
                        continue;

                    seen[Normalize(file)] = true;
                    files.Add(file);
                }
            }

            return files.ToArray();
        }
    }
}
