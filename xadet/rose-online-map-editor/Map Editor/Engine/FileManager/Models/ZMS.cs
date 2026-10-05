using System.IO;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using System.Windows.Forms;

namespace Map_Editor.Engine.Models
{
    /// <summary>
    /// ZMS class.
    /// </summary>
    public class ZMS
    {
        /// <summary>
        /// ZMS Vertex structure.
        /// </summary>
        public struct Vertex
        {
            /// <summary>
            /// Size of the vertex.
            /// </summary>
            public const int SIZE_IN_BYTES = sizeof(float) * 11;

            /// <summary>
            /// Vertex elements.
            /// </summary>
            public static readonly VertexElement[] VertexElements =
            {
                new VertexElement( 0, 0, VertexElementFormat.Vector3, VertexElementMethod.Default, VertexElementUsage.Position, 0 ),
                new VertexElement( 0, sizeof(float) * 3, VertexElementFormat.Vector3, VertexElementMethod.Default, VertexElementUsage.Normal, 0 ),
                new VertexElement( 0, sizeof(float) * 6, VertexElementFormat.Vector2, VertexElementMethod.Default, VertexElementUsage.TextureCoordinate, 0 ),
                new VertexElement( 0, sizeof(float) * 8, VertexElementFormat.Vector2, VertexElementMethod.Default, VertexElementUsage.TextureCoordinate, 1 ),
                new VertexElement( 0, sizeof(float) * 10, VertexElementFormat.Color, VertexElementMethod.Default, VertexElementUsage.Color, 0 )
            };

            #region Member Declarations

            /// <summary>
            /// Gets or sets the position.
            /// </summary>
            /// <value>The position.</value>
            public Vector3 Position { get; set; }

            /// <summary>
            /// Gets or sets the normal.
            /// </summary>
            /// <value>The normal.</value>
            public Vector3 Normal { get; set; }

            /// <summary>
            /// Gets or sets the texture coordinate.
            /// </summary>
            /// <value>The texture coordinate.</value>
            public Vector2 TextureCoordinate { get; set; }

            /// <summary>
            /// Gets or sets the lightmap coordinate.
            /// </summary>
            /// <value>The lightmap coordinate.</value>
            public Vector2 LightmapCoordinate { get; set; }

            /// <summary>
            /// Gets or sets the alpha.
            /// </summary>
            /// <value>The alpha.</value>
            public Color Alpha { get; set; }

            #endregion
        };

        #region Member Declarations

        /// <summary>
        /// Gets or sets the file path.
        /// </summary>
        /// <value>The file path.</value>
        public string FilePath { get; set; }

        /// <summary>
        /// Gets or sets the vertices.
        /// </summary>
        /// <value>The vertices.</value>
        public Vertex[] Vertices { get; set; }

        /// <summary>
        /// Gets or sets the indices.
        /// </summary>
        /// <value>The indices.</value>
        public short[] Indices { get; set; }

        /// <summary>
        /// Gets or sets the vertex count.
        /// </summary>
        /// <value>The vertex count.</value>
        public short VertexCount { get; set; }

        /// <summary>
        /// Gets or sets the index count.
        /// </summary>
        /// <value>The index count.</value>
        public short IndexCount { get; set; }

        /// <summary>
        /// Gets the format version from the magic (ZMS0008 -> 8), 0 if unrecognised.
        /// Version 7 and later store positions in metres; earlier ones in centimetres.
        /// </summary>
        public int Version { get; private set; }

        /// <summary>
        /// Gets the mesh bone table: a vertex's bone index is a slot in this table,
        /// and the value is the skeleton (ZMD) bone it means.
        /// </summary>
        public short[] BoneTable { get; private set; }

        /// <summary>
        /// Gets the per-vertex bone weights, or null if the mesh is not skinned.
        /// </summary>
        public Vector4[] BoneWeights { get; private set; }

        /// <summary>
        /// Gets the per-vertex bone table slots (4 per vertex), or null if the mesh is not skinned.
        /// </summary>
        public short[] BoneIndices { get; private set; }

        #endregion

        /// <summary>
        /// Initializes a new instance of the <see cref="ZMS"/> class.
        /// </summary>
        public ZMS()
        {

        }

        /// <summary>
        /// Initializes a new instance of the <see cref="ZMS"/> class.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public ZMS(string filePath)
        {
            Load(filePath);
        }

        /// <summary>
        /// Loads the specified file.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public void Load(string filePath)
        {
            FileHandler fh = new FileHandler(FilePath = filePath, FileHandler.FileOpenMode.Reading, null);

            byte[] magic = fh.Read<byte[]>(8);
            int version;

            Version = (magic[0] == 'Z' && magic[1] == 'M' && magic[2] == 'S' &&
                       int.TryParse(System.Text.Encoding.ASCII.GetString(magic, 3, 4), out version)) ? version : 0;

            int format = fh.Read<int>();

            fh.Seek(24, SeekOrigin.Current);

            short boneCount = fh.Read<short>();

            BoneTable = new short[System.Math.Max(0, (int)boneCount)];

            for (int i = 0; i < BoneTable.Length; i++)
                BoneTable[i] = fh.Read<short>();

            VertexCount = fh.Read<short>();

            Vertices = new Vertex[VertexCount];

            if ((format & 2) > 0)
            {
                for (int i = 0; i < VertexCount; i++)
                {
                    Vertices[i].Position = fh.Read<Vector3>();
                    Vertices[i].Alpha = new Color(0, 0, 0, 255);
                }
            }

            if ((format & 4) > 0)
            {
                for (int i = 0; i < VertexCount; i++)
                    Vertices[i].Normal = fh.Read<Vector3>();
            }

            if ((format & 8) > 0)
                fh.Seek(4 * VertexCount, SeekOrigin.Current);

            if ((format & 16) > 0 && (format & 32) > 0)
            {
                // Interleaved per vertex: 4 float weights, then 4 short bone table slots.
                BoneWeights = new Vector4[VertexCount];
                BoneIndices = new short[VertexCount * 4];

                for (int i = 0; i < VertexCount; i++)
                {
                    BoneWeights[i] = new Vector4(fh.Read<float>(), fh.Read<float>(), fh.Read<float>(), fh.Read<float>());

                    for (int j = 0; j < 4; j++)
                        BoneIndices[i * 4 + j] = fh.Read<short>();
                }
            }

            if ((format & 64) > 0)
                fh.Seek(12 * VertexCount, SeekOrigin.Current);

            int uvMaps = 0;

            if ((format & 128) > 0) uvMaps++;
            if ((format & 256) > 0) uvMaps++;
            if ((format & 512) > 0) uvMaps++;
            if ((format & 1024) > 0) uvMaps++;

            if (uvMaps >= 1)
            {
                for (int i = 0; i < VertexCount; i++)
                    Vertices[i].TextureCoordinate = fh.Read<Vector2>();
            }

            if (uvMaps >= 2)
            {
                for (int i = 0; i < VertexCount; i++)
                    Vertices[i].LightmapCoordinate = fh.Read<Vector2>();
            }

            IndexCount = fh.Read<short>();

            Indices = new short[IndexCount * 3];

            for (int i = 0; i < IndexCount; i++)
            {
                Indices[i * 3 + 0] = fh.Read<short>();
                Indices[i * 3 + 1] = fh.Read<short>();
                Indices[i * 3 + 2] = fh.Read<short>();
            }

            fh.Close();
        }

        /// <summary>
        /// Creates the vertex buffer.
        /// </summary>
        /// <param name="device">The device.</param>
        /// <returns>The vertex buffer.</returns>
        public VertexBuffer CreateVertexBuffer(GraphicsDevice device)
        {
            VertexBuffer vertexBuffer = new VertexBuffer(device, VertexCount * Vertex.SIZE_IN_BYTES, BufferUsage.WriteOnly);
            vertexBuffer.SetData(Vertices);

            return vertexBuffer;
        }

        /// <summary>
        /// Creates the dynamic vertex buffer.
        /// </summary>
        /// <param name="device">The device.</param>
        /// <returns>The vertex buffer.</returns>
        public DynamicVertexBuffer CreateDynamicVertexBuffer(GraphicsDevice device)
        {
            DynamicVertexBuffer vertexBuffer = new DynamicVertexBuffer(device, VertexCount * Vertex.SIZE_IN_BYTES, BufferUsage.WriteOnly);
            vertexBuffer.SetData(Vertices);

            return vertexBuffer;
        }

        /// <summary>
        /// Creates the index buffer.
        /// </summary>
        /// <param name="device">The device.</param>
        /// <returns>The index buffer.</returns>
        public IndexBuffer CreateIndexBuffer(GraphicsDevice device)
        {
            IndexBuffer indexBuffer = new IndexBuffer(device, typeof(short), sizeof(short) * IndexCount * 3, BufferUsage.WriteOnly);
            indexBuffer.SetData(Indices);

            return indexBuffer;
        }
    }
}