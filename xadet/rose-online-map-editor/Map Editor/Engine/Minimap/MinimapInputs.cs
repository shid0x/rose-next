using System;
using System.Collections.Generic;
using Map_Editor.Engine.Data;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;

namespace Map_Editor.Engine.Minimap
{
    /// <summary>
    /// Everything the styling needs from the game data, gathered on the
    /// editor's thread (Tools > Make minimap and the --shots `minimap`
    /// directive share it).
    /// </summary>
    public static class MinimapInputs
    {
        /// <summary>A retail minimap the parchment frame is cut from.</summary>
        public const string FRAME_SOURCE = "3DDATA\\MAPS\\JUNON\\JPT01\\MINIMAP.DDS";
        public const string OCEAN_TEXTURE = "3DDATA\\JUNON\\WATER\\OCEAN01_01.DDS";

        /// <summary>The zone's original minimap (before any Install), if it has one of the right size.</summary>
        public static byte[] Original(MinimapZone zone)
        {
            if (!zone.HasMinimap)
                return null;

            int w, h;
            byte[] o = MinimapZone.ReadOriginal(zone.MinimapPath, out w, out h);
            return o != null && w == zone.Width && h == zone.Height ? o : null;
        }

        public static MinimapStyleInput Gather(MinimapZone zone, MinimapRender render, byte[] original,
                                               bool matchOriginal, bool labels, bool waves)
        {
            MinimapStyleInput input = new MinimapStyleInput
            {
                Render = render,
                X0 = zone.X0,
                YTop = zone.YTop,
                Original = original,
                MatchOriginal = matchOriginal,
                Labels = labels,
                WaterTexture = waves
            };

            try
            {
                int w, h;
                byte[] frame = MinimapZone.ReadOriginal(FRAME_SOURCE, out w, out h);
                if (frame == null && original != null)
                {
                    frame = original;
                    w = zone.Width;
                    h = zone.Height;
                }
                input.Frame = frame;
                input.FrameWidth = w;
                input.FrameHeight = h;

                if (GameData.Exists(OCEAN_TEXTURE))
                    input.Ocean = DdsCodec.Decode(GameData.ReadAllBytes(OCEAN_TEXTURE), out input.OceanWidth, out input.OceanHeight);
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: frame or ocean texture unreadable, going without", ex);
            }

            if (labels)
                input.Gates = Gates();
            return input;
        }

        /// <summary>The warp gates of the open map and where each leads (WARP.STB -> LIST_ZONE -> its name).</summary>
        public static List<MinimapGate> Gates()
        {
            List<MinimapGate> gates = new List<MinimapGate>();
            if (MapManager.WarpGates == null || MapManager.WarpGates.WorldObjects == null)
                return gates;

            STB warp = null;
            try
            {
                if (GameData.Exists("3DDATA\\STB\\WARP.STB"))
                    warp = new STB("3DDATA\\STB\\WARP.STB");
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: WARP.STB unreadable, gates go unnamed", ex);
            }

            STB zones = FileManager.STBs["LIST_ZONE"];
            foreach (Events.WarpGates.WorldObject gate in MapManager.WarpGates.WorldObjects)
            {
                if (gate == null || gate.Entry == null)
                    continue;

                // editor cells are game column + 1: WARP col 1 = destination, LIST_ZONE col 26 = name key, col 0 = name
                string name = null;
                int id = gate.Entry.WarpID, dest;
                if (warp != null && id >= 0 && id < warp.Cells.Count && warp.Cells[id].Count > 2
                    && int.TryParse(warp.Cells[id][2].Trim(), out dest) && dest >= 0 && dest < zones.Cells.Count)
                {
                    string key = zones.Cells[dest].Count > 27 ? zones.Cells[dest][27].Trim() : "";
                    name = key.Length > 0 ? FileManager.STLs["LIST_ZONE_S"].Search(key) : null;
                    if (string.IsNullOrEmpty(name) || name == key)
                        name = zones.Cells[dest][1].Trim();
                }

                Vector3 p = gate.World.Translation;
                gates.Add(new MinimapGate { X = p.X, Y = p.Y, Name = name });
            }
            return gates;
        }
    }
}
