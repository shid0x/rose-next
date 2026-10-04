using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Map_Editor.Misc;

namespace Map_Editor.Engine
{
    /// <summary>
    /// A client's packed data: a data.idx index and the .vfs archives it names.
    /// </summary>
    /// <remarks>
    /// The index layout is the one triggervfs reads (and scripts/verify-vfs.py mirrors):
    /// base version, current version, archive count, then per archive a name and the
    /// offset of its file table; each table is a file count, a deleted count, a first
    /// offset, then per file a name, offset, size, block size, deleted / compressed /
    /// encrypted flags, version and checksum. Offsets are unsigned (archives up to 4 GB).
    ///
    /// The index flags never say whether a client scrambles its files, so the cipher is
    /// detected by decoding a sample of files whose first bytes are known (STB, ZMS, ZMO,
    /// ZMD, DDS) with each cipher in <see cref="VfsCipher.All"/> and keeping the one that
    /// reads them.
    /// </remarks>
    public class VfsArchive
    {
        /// <summary>
        /// One file in the archives.
        /// </summary>
        public class Entry
        {
            public string Path;
            public int Archive;
            public long Offset;
            public int Size;
        }

        /// <summary>
        /// Gets the index file path.
        /// </summary>
        public string IndexPath { get; private set; }

        /// <summary>
        /// Gets the cipher the archives are read with.
        /// </summary>
        public VfsCipher Cipher { get; private set; }

        /// <summary>
        /// Gets the files, keyed by <see cref="GameData.Normalize"/>d path.
        /// </summary>
        public Dictionary<string, Entry> Files { get; private set; }

        /// <summary>
        /// Gets the file names in each folder, keyed by normalized folder path.
        /// </summary>
        public Dictionary<string, List<string>> Folders { get; private set; }

        private readonly List<FileStream> archives = new List<FileStream>();
        private readonly List<string> archiveNames = new List<string>();

        /// <summary>
        /// Opens the index and every archive it names that exists next to it.
        /// </summary>
        /// <param name="indexPath">The data.idx path.</param>
        public VfsArchive(string indexPath)
        {
            IndexPath = indexPath;
            Files = new Dictionary<string, Entry>();
            Folders = new Dictionary<string, List<string>>();

            string folder = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(indexPath));
            int skippedCompressed = 0;

            using (BinaryReader reader = new BinaryReader(File.OpenRead(indexPath)))
            {
                reader.ReadInt32();                     // base version
                reader.ReadInt32();                     // current version
                int archiveCount = reader.ReadInt32();

                if (archiveCount <= 0 || archiveCount > 1000)
                    throw new InvalidDataException(string.Format("{0}: implausible archive count {1}; not a data.idx", indexPath, archiveCount));

                string[] names = new string[archiveCount];
                long[] tables = new long[archiveCount];

                for (int i = 0; i < archiveCount; i++)
                {
                    names[i] = ReadString(reader);
                    tables[i] = reader.ReadUInt32();
                }

                for (int i = 0; i < archiveCount; i++)
                {
                    string archivePath = FindFile(folder, names[i]);

                    if (archivePath == null)
                    {
                        Output.WriteLine(Output.MessageType.Normal, string.Format("- {0} names {1}, which is not there; skipped", System.IO.Path.GetFileName(indexPath), names[i]));
                        continue;
                    }

                    int archive = archives.Count;
                    archives.Add(new FileStream(archivePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite));
                    archiveNames.Add(names[i]);

                    reader.BaseStream.Seek(tables[i], SeekOrigin.Begin);

                    int fileCount = reader.ReadInt32();
                    reader.ReadInt32();                 // deleted count
                    reader.ReadInt32();                 // first offset

                    for (int j = 0; j < fileCount; j++)
                    {
                        string path = ReadString(reader);
                        long offset = reader.ReadUInt32();
                        int size = (int)reader.ReadUInt32();
                        reader.ReadUInt32();            // block size
                        byte deleted = reader.ReadByte();
                        byte compressed = reader.ReadByte();
                        reader.ReadByte();              // encrypted (unused by every client we know)
                        reader.ReadUInt32();            // version
                        reader.ReadUInt32();            // checksum

                        if (deleted != 0)
                            continue;

                        if (compressed != 0)
                        {
                            skippedCompressed++;
                            continue;
                        }

                        string key = GameData.Normalize(path);

                        if (key.Length == 0)
                            continue;

                        Files[key] = new Entry { Path = path, Archive = archive, Offset = offset, Size = size };

                        int slash = key.LastIndexOf('\\');
                        string folderKey = slash < 0 ? "" : key.Substring(0, slash);
                        List<string> list;

                        if (!Folders.TryGetValue(folderKey, out list))
                            Folders.Add(folderKey, list = new List<string>());

                        list.Add(slash < 0 ? key : key.Substring(slash + 1));
                    }
                }
            }

            if (skippedCompressed > 0)
                Output.WriteLine(Output.MessageType.Error, string.Format("- {0}: {1} compressed files skipped (no client we know compresses; unsupported)", System.IO.Path.GetFileName(indexPath), skippedCompressed));

            DetectCipher();
        }

        /// <summary>
        /// Gets a summary of the mounted archives for the log.
        /// </summary>
        public string Summary
        {
            get { return string.Format("{0}: {1} archives ({2}), {3} files, {4}", IndexPath, archives.Count, string.Join(", ", archiveNames.ToArray()), Files.Count, Cipher.Name); }
        }

        /// <summary>
        /// Reads and decodes one file.
        /// </summary>
        /// <param name="entry">The entry.</param>
        /// <returns>The file's bytes.</returns>
        public byte[] Read(Entry entry)
        {
            return Cipher.Decode(ReadRaw(entry), entry.Path);
        }

        /// <summary>
        /// Reads one file as stored.
        /// </summary>
        private byte[] ReadRaw(Entry entry)
        {
            FileStream stream = archives[entry.Archive];
            byte[] data = new byte[entry.Size];

            // Map loading runs on a worker thread while the UI can open previews.
            lock (stream)
            {
                stream.Seek(entry.Offset, SeekOrigin.Begin);

                int read = 0;

                while (read < data.Length)
                {
                    int count = stream.Read(data, read, data.Length - read);

                    if (count <= 0)
                        throw new EndOfStreamException(string.Format("{0} is cut short in {1}", entry.Path, archiveNames[entry.Archive]));

                    read += count;
                }
            }

            return data;
        }

        /// <summary>
        /// Picks the cipher that reads a sample of files with known first bytes.
        /// </summary>
        private void DetectCipher()
        {
            List<Entry> probes = new List<Entry>();

            foreach (Entry entry in Files.Values)
            {
                if (entry.Size > 128 && entry.Size < 5 * 1024 * 1024 && VfsCipher.KnownSignature(entry.Path) != null)
                    probes.Add(entry);
            }

            // Spread the sample over the whole index rather than one folder.
            List<Entry> sample = new List<Entry>();
            int step = Math.Max(1, probes.Count / 24);

            for (int i = 0; i < probes.Count && sample.Count < 24; i += step)
                sample.Add(probes[i]);

            Cipher = VfsCipher.All[0];

            if (sample.Count == 0)
            {
                Output.WriteLine(Output.MessageType.Error, string.Format("- {0}: no file with a known signature to identify the cipher; reading as plain", System.IO.Path.GetFileName(IndexPath)));
                return;
            }

            List<byte[]> raw = new List<byte[]>();

            for (int i = 0; i < sample.Count; i++)
                raw.Add(ReadRaw(sample[i]));

            int bestScore = -1;

            for (int c = 0; c < VfsCipher.All.Length; c++)
            {
                int score = 0;

                for (int i = 0; i < sample.Count; i++)
                {
                    try
                    {
                        if (VfsCipher.HasSignature(VfsCipher.All[c].Decode(raw[i], sample[i].Path), sample[i].Path))
                            score++;
                    }
                    catch (InvalidDataException)
                    {
                    }
                }

                if (score > bestScore)
                {
                    bestScore = score;
                    Cipher = VfsCipher.All[c];
                }
            }

            Output.WriteLine(bestScore * 10 >= sample.Count * 8 ? Output.MessageType.Normal : Output.MessageType.Error,
                string.Format("- {0}: cipher {1} ({2}/{3} sample files read)", System.IO.Path.GetFileName(IndexPath), Cipher.Name, bestScore, sample.Count));
        }

        /// <summary>
        /// Reads a u16-length name, dropping the trailing NUL some writers include.
        /// </summary>
        private static string ReadString(BinaryReader reader)
        {
            int length = reader.ReadUInt16();
            byte[] bytes = reader.ReadBytes(length);
            string text = Encoding.Default.GetString(bytes);
            int nul = text.IndexOf('\0');

            return nul < 0 ? text : text.Substring(0, nul);
        }

        /// <summary>
        /// Finds an archive next to the index (Windows paths ignore case).
        /// </summary>
        private static string FindFile(string folder, string name)
        {
            string path = System.IO.Path.Combine(folder, name);

            return File.Exists(path) ? path : null;
        }
    }

    /// <summary>
    /// How a client stores the files in its .vfs archives.
    /// </summary>
    public abstract class VfsCipher
    {
        /// <summary>
        /// Every cipher we know, plain first (it wins a tie).
        /// </summary>
        public static readonly VfsCipher[] All = { new PlainCipher(), new WishCipher() };

        /// <summary>
        /// Gets the name for the log.
        /// </summary>
        public abstract string Name { get; }

        /// <summary>
        /// Decodes a file as stored.
        /// </summary>
        /// <param name="raw">The stored bytes.</param>
        /// <param name="path">The file's path in the index (some ciphers need its type).</param>
        /// <returns>The file.</returns>
        public abstract byte[] Decode(byte[] raw, string path);

        /// <summary>
        /// The first bytes a file of this type always starts with, or null.
        /// </summary>
        public static byte[] KnownSignature(string path)
        {
            switch (System.IO.Path.GetExtension(path).ToUpperInvariant())
            {
                case ".STB": return new byte[] { (byte)'S', (byte)'T', (byte)'B' };
                case ".ZMS": return new byte[] { (byte)'Z', (byte)'M', (byte)'S' };
                case ".ZMO": return new byte[] { (byte)'Z', (byte)'M', (byte)'O' };
                case ".ZMD": return new byte[] { (byte)'Z', (byte)'M', (byte)'D' };
                case ".DDS": return new byte[] { (byte)'D', (byte)'D', (byte)'S', (byte)' ' };
                default: return null;
            }
        }

        /// <summary>
        /// Whether decoded bytes start with their type's signature.
        /// </summary>
        public static bool HasSignature(byte[] data, string path)
        {
            byte[] signature = KnownSignature(path);

            if (signature == null || data.Length < signature.Length)
                return false;

            for (int i = 0; i < signature.Length; i++)
            {
                if (data[i] != signature[i])
                    return false;
            }

            return true;
        }
    }

    /// <summary>
    /// Files stored as they are (retail, iROSE, Rose Next, most private servers).
    /// </summary>
    public class PlainCipher : VfsCipher
    {
        public override string Name
        {
            get { return "plain"; }
        }

        public override byte[] Decode(byte[] raw, string path)
        {
            return raw;
        }
    }

    /// <summary>
    /// Wish Online (Taiwan, 2010): each file rotated and XORed with its own first byte.
    /// </summary>
    /// <remarks>
    /// For a file of n bytes, a rotation r is picked by size; the stored byte at r is the
    /// key (the file's first byte, kept as is) and file byte i (i &gt; 0) is stored at
    /// (i + r) mod n, XORed with the key. Files of 128 bytes or less are stored plain.
    /// The size buckets were measured on the whole client (about 12,000 files checked by
    /// signature); one 6 MB file uses a rotation past the last bucket, so files that large
    /// are found by searching for their signature.
    /// </remarks>
    public class WishCipher : VfsCipher
    {
        private static readonly int[] Limits = { 512, 1024, 5120, 10240, 51200, 102400, 512000, 1048576, 5242880 };
        private static readonly int[] Rotations = { 21, 258, 500, 3113, 7428, 20240, 66633, 112233, 456321 };

        public override string Name
        {
            get { return "Wish Online (rotate + XOR)"; }
        }

        public override byte[] Decode(byte[] raw, string path)
        {
            int n = raw.Length;

            if (n <= 128)
                return raw;

            for (int i = 0; i < Limits.Length; i++)
            {
                if (n < Limits[i])
                    return Unrotate(raw, Rotations[i]);
            }

            byte[] signature = KnownSignature(path);

            if (signature != null)
            {
                for (int rotation = 0; rotation < n; rotation++)
                {
                    if (raw[rotation] != signature[0] || (raw[(rotation + 1) % n] ^ raw[rotation]) != signature[1])
                        continue;

                    byte[] data = Unrotate(raw, rotation);

                    if (HasSignature(data, path))
                        return data;
                }
            }

            throw new InvalidDataException(string.Format("{0}: {1} bytes is past the Wish Online size buckets and its type has no known signature to find the rotation", path, n));
        }

        private static byte[] Unrotate(byte[] raw, int rotation)
        {
            int n = raw.Length;
            byte key = raw[rotation];
            byte[] data = new byte[n];

            data[0] = key;

            for (int i = 1; i < n; i++)
                data[i] = (byte)(raw[(i + rotation) % n] ^ key);

            return data;
        }
    }
}
