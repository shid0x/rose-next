using System;
using System.Collections.Generic;
using Map_Editor.Engine.Models;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;

namespace Map_Editor.Engine.Character
{
    /// <summary>
    /// Poses a character's skinned meshes the way the game first shows them: frame 0
    /// of the character's stand motion (LIST_NPC.CHR motion slot 0) on its skeleton.
    ///
    /// Without this the editor draws a skinned mesh's raw vertices, i.e. the bind pose
    /// the artist skinned it in. For most monsters that is close enough to standing;
    /// for rigs whose bind pose is not (a Biped rig used as a horse, a rider on custom
    /// bones) the parts come apart.
    ///
    /// The maths follows the engine: a bone's local transform is its ZMD bind
    /// rotation and translation, with the motion's channels replacing them where the
    /// motion has one (zz_bone::apply_channel_by_frame); a vertex moves by
    /// inverse(bind world) * posed world of each bone it is weighted to
    /// (zz_bone::get_boneTM). Done once on the CPU when the mesh is loaded.
    /// </summary>
    public class CharacterPose
    {
        #region Member Declarations

        /// <summary>
        /// Gets the cache key: skeleton and motion path.
        /// </summary>
        public string Key { get; private set; }

        private Matrix[] skin;

        #endregion

        // Shared by every manager; a null entry records a pose that could not be built,
        // so its problem is logged once.
        private static readonly Dictionary<string, CharacterPose> cache =
            new Dictionary<string, CharacterPose>(StringComparer.OrdinalIgnoreCase);

        private static readonly HashSet<string> reportedMeshes =
            new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        private CharacterPose()
        {
        }

        /// <summary>
        /// Gets the pose of a LIST_NPC character, or null when it has no skeleton or
        /// stand motion to pose with (the mesh is then drawn as before).
        /// </summary>
        /// <param name="characterID">The LIST_NPC row.</param>
        public static CharacterPose ForCharacter(int characterID)
        {
            CHR chr;

            if (FileManager.CHRs == null || !FileManager.CHRs.TryGetValue("LIST_NPC", out chr))
                return null;

            if (characterID < 0 || characterID >= chr.Characters.Count || !chr.Characters[characterID].IsActive)
                return null;

            CHR.Character character = chr.Characters[characterID];

            if (character.BoneID < 0 || character.BoneID >= chr.Bones.Count)
                return null;

            string motionPath = null;

            foreach (CHR.Character.Motion motion in character.Motions)
            {
                if (motion.ID == 0 && motion.MotionID >= 0 && motion.MotionID < chr.Motions.Count)
                {
                    motionPath = chr.Motions[motion.MotionID];
                    break;
                }
            }

            string skeletonPath = chr.Bones[character.BoneID];

            if (string.IsNullOrEmpty(skeletonPath) || string.IsNullOrEmpty(motionPath) ||
                skeletonPath.Trim().Length == 0 || motionPath.Trim().Length == 0)
                return null;

            string key = skeletonPath + "|" + motionPath;

            lock (cache)
            {
                CharacterPose pose;

                if (cache.TryGetValue(key, out pose))
                    return pose;

                pose = Build(key, skeletonPath, motionPath, characterID);
                cache[key] = pose;

                return pose;
            }
        }

        private static CharacterPose Build(string key, string skeletonPath, string motionPath, int characterID)
        {
            if (!GameData.Exists(skeletonPath) || !GameData.Exists(motionPath))
            {
                Output.WriteLine(Output.MessageType.Error, string.Format("Character {0} drawn unposed, missing {1}",
                    characterID, GameData.Exists(skeletonPath) ? motionPath : skeletonPath));
                return null;
            }

            try
            {
                ZMD skeleton = new ZMD(skeletonPath);
                ZMO motion = new ZMO(motionPath, false, true);

                int boneCount = skeleton.Bones.Length;

                if (boneCount == 0)
                    return null;

                Vector3[] translations = new Vector3[boneCount];
                Quaternion[] rotations = new Quaternion[boneCount];

                for (int i = 0; i < boneCount; i++)
                {
                    translations[i] = skeleton.Bones[i].Translation;
                    rotations[i] = skeleton.Bones[i].Rotation;
                }

                Matrix[] bind = WorldTransforms(skeleton, translations, rotations);

                if (motion.Frames != null && motion.Frames.Length > 0)
                {
                    for (int j = 0; j < motion.Channels.Length; j++)
                    {
                        int bone = motion.Channels[j].ID;

                        if (bone < 0 || bone >= boneCount)
                            continue;

                        if (motion.Channels[j].Type == ZMO.ChannelType.Position)
                            translations[bone] = motion.Frames[0].Channels[j].Position;
                        else if (motion.Channels[j].Type == ZMO.ChannelType.Rotation)
                            rotations[bone] = motion.Frames[0].Channels[j].Rotation;
                    }
                }

                Matrix[] posed = WorldTransforms(skeleton, translations, rotations);

                CharacterPose pose = new CharacterPose()
                {
                    Key = key,
                    skin = new Matrix[boneCount]
                };

                for (int i = 0; i < boneCount; i++)
                    pose.skin[i] = Matrix.Invert(bind[i]) * posed[i];

                return pose;
            }
            catch (Exception exception)
            {
                Output.WriteException(string.Format("Character {0} drawn unposed, cannot pose {1} with {2}",
                    characterID, skeletonPath, motionPath), exception);
                return null;
            }
        }

        /// <summary>
        /// Row-vector world transforms: local = rotation * translation, world = local * parent world.
        /// </summary>
        private static Matrix[] WorldTransforms(ZMD skeleton, Vector3[] translations, Quaternion[] rotations)
        {
            int boneCount = skeleton.Bones.Length;
            Matrix[] world = new Matrix[boneCount];
            bool[] done = new bool[boneCount];

            for (int i = 0; i < boneCount; i++)
                Resolve(i, skeleton, translations, rotations, world, done, 0);

            return world;
        }

        private static Matrix Resolve(int i, ZMD skeleton, Vector3[] translations, Quaternion[] rotations,
            Matrix[] world, bool[] done, int depth)
        {
            if (done[i])
                return world[i];

            Quaternion rotation = rotations[i];

            if (rotation.LengthSquared() < 1e-12f)
                rotation = Quaternion.Identity;
            else
                rotation.Normalize();

            Matrix local = Matrix.CreateFromQuaternion(rotation) * Matrix.CreateTranslation(translations[i]);
            int parent = skeleton.Bones[i].Parent;

            // The root names itself; a broken parent link (or a cycle) is treated as a root.
            if (parent == i || parent < 0 || parent >= world.Length || depth > world.Length)
                world[i] = local;
            else
                world[i] = local * Resolve(parent, skeleton, translations, rotations, world, done, depth + 1);

            done[i] = true;

            return world[i];
        }

        /// <summary>
        /// Moves a loaded mesh's vertices into this pose. Leaves the mesh untouched and
        /// returns false when it cannot be posed (not skinned, centimetre-unit ZMS6 or
        /// older, or a bone reference outside the skeleton).
        /// </summary>
        /// <param name="mesh">The mesh.</param>
        public bool Apply(ZMS mesh)
        {
            if (mesh == null || mesh.Version < 7 || mesh.BoneWeights == null || mesh.BoneTable == null ||
                mesh.BoneTable.Length == 0 || mesh.Vertices == null)
                return false;

            // Validate every reference before moving anything.
            for (int i = 0; i < mesh.BoneIndices.Length; i++)
            {
                float weight = Component(mesh.BoneWeights[i / 4], i % 4);

                if (weight == 0.0f)
                    continue;

                int slot = mesh.BoneIndices[i];

                if (slot < 0 || slot >= mesh.BoneTable.Length || mesh.BoneTable[slot] < 0 || mesh.BoneTable[slot] >= skin.Length)
                {
                    lock (reportedMeshes)
                    {
                        if (reportedMeshes.Add(mesh.FilePath + "|" + Key))
                            Output.WriteLine(Output.MessageType.Error, string.Format("Mesh {0} drawn unposed, bone slot {1} is outside {2}",
                                mesh.FilePath, slot, Key));
                    }

                    return false;
                }
            }

            for (int i = 0; i < mesh.VertexCount; i++)
            {
                Vector3 position = Vector3.Zero;
                Vector3 normal = Vector3.Zero;
                float total = 0.0f;

                for (int j = 0; j < 4; j++)
                {
                    float weight = Component(mesh.BoneWeights[i], j);

                    if (weight == 0.0f)
                        continue;

                    Matrix bone = skin[mesh.BoneTable[mesh.BoneIndices[i * 4 + j]]];

                    position += Vector3.Transform(mesh.Vertices[i].Position, bone) * weight;
                    normal += Vector3.TransformNormal(mesh.Vertices[i].Normal, bone) * weight;
                    total += weight;
                }

                if (total <= 0.0f)
                    continue;

                mesh.Vertices[i].Position = position / total;

                if (normal.LengthSquared() > 0.0f)
                    mesh.Vertices[i].Normal = Vector3.Normalize(normal);
            }

            return true;
        }

        private static float Component(Vector4 value, int index)
        {
            switch (index)
            {
                case 0: return value.X;
                case 1: return value.Y;
                case 2: return value.Z;
                default: return value.W;
            }
        }
    }
}
