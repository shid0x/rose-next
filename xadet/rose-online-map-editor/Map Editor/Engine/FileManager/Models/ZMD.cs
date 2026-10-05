using System.Collections.Generic;
using System.IO;
using System.Text;
using Microsoft.Xna.Framework;

namespace Map_Editor.Engine.Models
{
    /// <summary>
    /// ZMD class: a character skeleton. Only the bones are read; the dummy points
    /// that follow them are not needed to pose a mesh.
    /// </summary>
    public class ZMD
    {
        public struct Bone
        {
            #region Member Declarations

            /// <summary>
            /// Gets or sets the parent bone; the root names itself.
            /// </summary>
            public int Parent { get; set; }

            public string Name { get; set; }

            /// <summary>
            /// Gets or sets the bind translation relative to the parent, in metres
            /// (the file stores centimetres; the engine scales by ZZ_SCALE_IN).
            /// </summary>
            public Vector3 Translation { get; set; }

            /// <summary>
            /// Gets or sets the bind rotation relative to the parent.
            /// </summary>
            public Quaternion Rotation { get; set; }

            #endregion
        }

        #region Member Declarations

        public string FilePath { get; private set; }

        public Bone[] Bones { get; private set; }

        #endregion

        public ZMD(string filePath)
        {
            Load(filePath);
        }

        /// <summary>
        /// Loads the specified file, as zz_skeleton::load_skeleton does.
        /// </summary>
        /// <param name="filePath">The file path.</param>
        public void Load(string filePath)
        {
            FileHandler fh = new FileHandler(FilePath = filePath, FileHandler.FileOpenMode.Reading, null);

            try
            {
                string magic = Encoding.ASCII.GetString(fh.Read<byte[]>(7));

                if (magic != "ZMD0002" && magic != "ZMD0003")
                    throw new InvalidDataException(string.Format("{0}: not a ZMD skeleton ({1})", filePath, magic));

                int boneCount = fh.Read<int>();

                if (boneCount < 0 || boneCount > 1000)
                    throw new InvalidDataException(string.Format("{0}: implausible bone count {1}", filePath, boneCount));

                Bones = new Bone[boneCount];

                for (int i = 0; i < boneCount; i++)
                {
                    int parent = fh.Read<int>();

                    List<byte> name = new List<byte>();
                    byte character;

                    while ((character = fh.Read<byte>()) != 0)
                        name.Add(character);

                    Vector3 translation = fh.Read<Vector3>() / 100.0f;

                    // Stored w, x, y, z.
                    float w = fh.Read<float>();
                    float x = fh.Read<float>();
                    float y = fh.Read<float>();
                    float z = fh.Read<float>();

                    Bones[i] = new Bone()
                    {
                        Parent = parent,
                        Name = Encoding.Default.GetString(name.ToArray()),
                        Translation = translation,
                        Rotation = new Quaternion(x, y, z, w)
                    };
                }
            }
            finally
            {
                fh.Close();
            }
        }
    }
}
