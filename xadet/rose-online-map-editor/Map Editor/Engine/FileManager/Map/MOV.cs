using System.IO;

namespace Map_Editor.Engine.Map
{
    // Server format: width, height, then rows from south to north.
    // Zero permits AI movement; every nonzero byte blocks it. Preserve raw values.
    public class MOV
    {
        public const int Size = 32;
        public byte[,] IsWalkable { get; set; }
        public string FilePath { get; set; }
        public MOV() { IsWalkable = new byte[Size, Size]; }
        public MOV(string path) { Load(path); }
        public void Load(string path)
        {
            using (BinaryReader reader = new BinaryReader(GameData.OpenRead(path)))
            {
                if (reader.BaseStream.Length != 8 + Size * Size ||
                    reader.ReadInt32() != Size || reader.ReadInt32() != Size)
                    throw new InvalidDataException("Expected a 32 x 32 movement grid: " + path);
                byte[,] cells = new byte[Size, Size];
                for (int y = 0; y < Size; y++)
                    for (int x = 0; x < Size; x++) cells[y, x] = reader.ReadByte();
                IsWalkable = cells;
                FilePath = path;
            }
        }
        public void Save() { Save(FilePath); }
        public void Save(string path)
        {
            if (IsWalkable == null || IsWalkable.GetLength(0) != Size || IsWalkable.GetLength(1) != Size)
                throw new InvalidDataException("Movement grids must be 32 x 32.");
            using (BinaryWriter writer = new BinaryWriter(File.Create(path)))
            {
                writer.Write(Size);
                writer.Write(Size);
                for (int y = 0; y < Size; y++)
                    for (int x = 0; x < Size; x++) writer.Write(IsWalkable[y, x]);
            }
            FilePath = path;
        }
    }
}
