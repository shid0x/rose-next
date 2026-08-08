using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Map_Editor.Engine.Data
{
    /// <summary>
    /// STB class.
    /// </summary>
    public class STB
    {
        #region Member Declarations

        /// <summary>
        /// Gets or sets the size of the row.
        /// </summary>
        /// <value>The size of the row.</value>
        public int RowSize { get; set; }

        /// <summary>
        /// Gets or sets the file path.
        /// </summary>
        /// <value>The file path.</value>
        public string FilePath { get; set; }

        #endregion

        #region List Declarations

        /// <summary>
        /// Gets or sets the column sizes.
        /// </summary>
        /// <value>The column sizes.</value>
        public List<short> ColumnSizes { get; set; }

        /// <summary>
        /// Gets or sets the column names.
        /// </summary>
        /// <value>The column names.</value>
        public List<string> ColumnNames { get; set; }

        /// <summary>
        /// Gets or sets the cells.
        /// </summary>
        /// <value>The cells.</value>
        public List<List<string>> Cells { get; set; }

        #endregion

        /// <summary>
        /// Initializes a new instance of the <see cref="STB"/> class.
        /// </summary>
        public STB()
        {
            ColumnSizes = new List<short>();
            ColumnNames = new List<string>();
            Cells = new List<List<string>>();
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="STB"/> class.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public STB(string filePath)
        {
            Load(filePath);
        }

        /// <summary>
        /// Checks the file really is an STB before the reader walks off the end of it.
        /// </summary>
        /// <remarks>
        /// Some private-server data sets ship scrambled or placeholder tables (QQ-iROSE
        /// encrypts 3DDATA\TERRAIN\TILES\ZONETYPEINFO.STB, which is editor-only data).
        /// Parsing one of those produces a junk row/column count and the reader dies with
        /// a bare EndOfStreamException that names no file, so fail early and say which.
        /// </remarks>
        /// <param name="filePath">The file path.</param>
        private static void VerifySignature(string filePath)
        {
            byte[] signature = new byte[4];

            using (FileStream stream = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                if (stream.Read(signature, 0, 4) < 4)
                    throw new InvalidDataException(string.Format("{0} is too small to be an STB file.", filePath));
            }

            if (signature[0] == 'S' && signature[1] == 'T' && signature[2] == 'B')
                return;

            throw new InvalidDataException(string.Format("{0} is not an STB file (signature {1:X2} {2:X2} {3:X2} {4:X2}); it is encrypted, compressed or corrupt.", filePath, signature[0], signature[1], signature[2], signature[3]));
        }

        /// <summary>
        /// Loads the specified file.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public void Load(string filePath)
        {
            VerifySignature(filePath);

            FileHandler fh = new FileHandler(FilePath = filePath, FileHandler.FileOpenMode.Reading, Encoding.GetEncoding("EUC-KR"));

            fh.Read<BaseString>(4);

            int offset = fh.Read<int>();

            int rowCount = fh.Read<int>();
            int columnCount = fh.Read<int>();
            RowSize = fh.Read<int>();

            ColumnSizes = new List<short>(columnCount + 1);

            for (int i = 0; i < columnCount + 1; i++)
                ColumnSizes.Add(fh.Read<short>());

            ColumnNames = new List<string>(columnCount + 1);

            for (int i = 0; i < columnCount + 1; i++)
                ColumnNames.Add(fh.Read<string>(fh.Read<short>()));

            Cells = new List<List<string>>(rowCount);

            for (int i = 0; i < rowCount - 1; i++)
            {
                Cells.Add(new List<string>());

                Cells[i].Add(fh.Read<string>(fh.Read<short>()));
            }

            for (int i = 0; i < rowCount - 1; i++)
            {
                for (int j = 0; j < columnCount - 1; j++)
                    Cells[i].Add(fh.Read<string>(fh.Read<short>()));
            }

            fh.Close();
        }

        /// <summary>
        /// Saves the file.
        /// </summary>
        public void Save()
        {
            Save(FilePath);
        }

        /// <summary>
        /// Saves the specified file.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public void Save(string filePath)
        {
            Encoding encoding = Encoding.GetEncoding("EUC-KR");

            FileHandler fh = new FileHandler(FilePath = filePath, FileHandler.FileOpenMode.Writing, encoding);

            fh.Write<BaseString>("STB1");
            fh.Write<int>(0);

            int rowCount = Cells.Count;
            int columnCount = (Cells.Count > 0) ? Cells[0].Count : 0;

            fh.Write<int>(rowCount + 1);
            fh.Write<int>(columnCount);
            fh.Write<int>(RowSize);

            for (int i = 0; i < ColumnSizes.Count; i++)
                fh.Write<short>((short)ColumnSizes[i]);

            for (int i = 0; i < ColumnNames.Count; i++)
            {
                fh.Write<short>((short)encoding.GetByteCount(ColumnNames[i]));
                fh.Write<string>(ColumnNames[i]);
            }

            for (int i = 0; i < rowCount; i++)
            {
                fh.Write<short>((short)encoding.GetByteCount(Cells[i][0]));
                fh.Write<string>(Cells[i][0]);
            }

            int dataOffset = fh.Tell();

            for (int i = 0; i < rowCount; i++)
            {
                for (int j = 1; j < columnCount; j++)
                {
                    fh.Write<short>((short)encoding.GetByteCount(Cells[i][j]));
                    fh.Write<string>(Cells[i][j]);
                }
            }

            fh.Seek(4, SeekOrigin.Begin);
            fh.Write<int>(dataOffset);

            fh.Close();
        }
    }
}