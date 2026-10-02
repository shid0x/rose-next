// Editor oracle for mapgen phase 1.
//
// Builds the zone folder the xadet editor's File > New would write for the
// given size and tile file, using the editor's OWN writers (ZON/IFO/TIL/HIM/
// MOV/LIT.Save from "Map Editor.exe"). The object construction below mirrors
// Forms/New.xaml.cs:237-480 line for line. Two things are left out:
//
//   * the 512x512 plane lightmap DDS, which needs an XNA GraphicsDevice;
//   * the LIST_ZONE / LIST_ZONE_S edits.
//
// scripts/mapgen-zone.py oracle compares this against mapgen's own output for
// the same inputs.
//
// Usage: NewZoneOracle.exe <data dir> <out dir> <sizeX> <sizeY> <tile-file ZON, relative to data> <zone file name>
//        NewZoneOracle.exe load <zone folder>
//            reads every ZON/HIM/TIL/IFO/MOV/LIT in the folder with the editor's own
//            readers and prints what it got: the editor-side load check without the GUI.

using System;
using System.Collections.Generic;
using System.IO;
using Map_Editor.Engine.Map;

class NewZoneOracle
{
    static int Load(string dir)
    {
        int failed = 0;
        foreach (string path in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
        {
            string ext = Path.GetExtension(path).ToUpperInvariant();
            string name = path.Substring(dir.Length).TrimStart('\\');
            try
            {
                string what;
                switch (ext)
                {
                    case ".ZON":
                        ZON z = new ZON(path);
                        what = string.Format("type {0}, {1}x{2}, grid {3}/{4}, {5} events, {6} textures, {7} tiles, economy '{8}'",
                            z.ZoneInfo.ZoneType, z.ZoneInfo.ZoneWidth, z.ZoneInfo.ZoneHeight, z.ZoneInfo.GridCount,
                            z.ZoneInfo.GridSize, z.SpawnPoints.Count, z.Textures.Count, z.Tiles.Count, z.EconomyInfo.AreaName);
                        foreach (ZON.SpawnPoint sp in z.SpawnPoints)
                            what += string.Format("; {0} @ ({1}, {2}, {3}) m", sp.Name, sp.Position.X, sp.Position.Y, sp.Position.Z);
                        break;
                    case ".HIM":
                        HIM h = new HIM(path);
                        what = string.Format("{0}x{1} heights, [0,0] = {2} m", h.Position.GetLength(0), h.Position.GetLength(1), h.Position[0, 0]);
                        break;
                    case ".TIL":
                        TIL t = new TIL(path);
                        what = string.Format("{0}x{1}, tile[0,0] brush {2} set {3} index {4} id {5}", t.Tiles.GetLength(0), t.Tiles.GetLength(1),
                            t.Tiles[0, 0].BrushID, t.Tiles[0, 0].TileSetNumber, t.Tiles[0, 0].TileIndex, t.Tiles[0, 0].TileID);
                        break;
                    case ".IFO":
                        IFO i = new IFO(path);
                        what = string.Format("map {0} cell {1},{2}; deco {3}, cnst {4}, water planes {5}, wide water {6}x{7}",
                            i.MapInfo.MapName, i.MapInfo.MapCellX, i.MapInfo.MapCellY, i.Decoration.Count,
                            i.Construction.Count, i.Water.Count, i.WideWater.X, i.WideWater.Y);
                        break;
                    case ".MOV":
                        MOV m = new MOV(path);
                        what = string.Format("{0}x{1}", m.IsWalkable.GetLength(0), m.IsWalkable.GetLength(1));
                        break;
                    case ".LIT":
                        LIT l = new LIT(path, null);
                        what = string.Format("{0} objects, {1} dds", l.Objects.Count, l.DDSFiles.Count);
                        break;
                    default:
                        continue;
                }
                Console.WriteLine("  ok   {0,-44} {1}", name, what);
            }
            catch (Exception e)
            {
                failed++;
                Console.WriteLine("  FAIL {0,-44} {1}", name, e.Message);
            }
        }
        Console.WriteLine(failed == 0 ? "editor readers: all files loaded" : string.Format("editor readers: {0} file(s) FAILED", failed));
        return failed == 0 ? 0 : 1;
    }

    static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "load")
            return Load(Path.GetFullPath(args[1]));
        if (args.Length != 6)
        {
            Console.Error.WriteLine("usage: NewZoneOracle <data dir> <out dir> <sizeX> <sizeY> <tile ZON> <zone file>");
            return 2;
        }
        string outDir = Path.GetFullPath(args[1]);
        Directory.SetCurrentDirectory(args[0]);   // TileFile paths are relative to data, as in the editor
        int sizeX = int.Parse(args[2]), sizeY = int.Parse(args[3]);
        string tileFile = args[4], zoneFile = args[5];
        Directory.CreateDirectory(outDir);

        LIT emptyLIT = new LIT() { DDSFiles = new List<LIT.DDS>(0), Objects = new List<LIT.Object>(0) };

        IFO emptyIFO = new IFO()
        {
            MapInfo = new IFO.MapInformation(),
            Decoration = new List<IFO.BaseIFO>(0),
            NPCs = new List<IFO.NPC>(0),
            Construction = new List<IFO.BaseIFO>(0),
            Sounds = new List<IFO.Sound>(0),
            Effects = new List<IFO.Effect>(0),
            Animation = new List<IFO.BaseIFO>(0),
            WideWater = new IFO.UnusedWater() { WaterBlocks = new IFO.UnusedWater.WaterBlock[16, 16], X = 16, Y = 16 },
            Monsters = new List<IFO.MonsterSpawn>(0),
            Water = new List<IFO.WaterBlock>(0),
            WarpGates = new List<IFO.BaseIFO>(0),
            Collision = new List<IFO.BaseIFO>(0),
            EventTriggers = new List<IFO.EventTrigger>(0)
        };
        for (int x = 0; x < 16; x++)
            for (int y = 0; y < 16; y++)
                emptyIFO.WideWater.WaterBlocks[x, y] = new IFO.UnusedWater.WaterBlock()
                { Use = 0, Height = 0, WaterType = 1, WaterIndex = 0, Reserved = 0 };

        TIL emptyTIL = new TIL() { Tiles = new TIL.Tile[16, 16] };
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                emptyTIL.Tiles[y, x] = new TIL.Tile() { BrushID = 1, TileIndex = 15, TileSetNumber = 1, TileID = 1 };

        MOV emptyMOV = new MOV() { IsWalkable = new byte[32, 32] };
        HIM emptyHIM = new HIM() { GridCount = 4, GridSize = 250, Position = new float[65, 65] };

        ZON temporaryZON = new ZON()
        {
            Blocks = new List<ZON.Block>(5),
            ZoneInfo = new ZON.Block0()
            {
                ZoneType = 0, ZoneWidth = 64, ZoneHeight = 64, GridCount = 4, GridSize = 250,
                XCount = 30, YCount = 30, ZoneParts = new ZON.Block0.ZonePart[64, 64]
            },
            SpawnPoints = new List<ZON.SpawnPoint>(2)
        };
        for (int i = 0; i < 5; i++)
            temporaryZON.Blocks.Add(new ZON.Block() { Type = (ZON.BlockType)i, Offset = 0 });
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                temporaryZON.ZoneInfo.ZoneParts[y, x] = new ZON.Block0.ZonePart()
                { UseMap = 0, Position = new Microsoft.Xna.Framework.Vector2() { X = 0.0f, Y = 0.0f } };
        temporaryZON.SpawnPoints.Add(new ZON.SpawnPoint()
        { Name = "start", Position = new Microsoft.Xna.Framework.Vector3(4930.0f, 5470.0f, 0.0f) });
        temporaryZON.SpawnPoints.Add(new ZON.SpawnPoint()
        { Name = "restore", Position = new Microsoft.Xna.Framework.Vector3(4910.0f, 5500.0f, 0.0f) });

        ZON copyingZON = new ZON();
        copyingZON.Load(tileFile);
        temporaryZON.Textures = new List<ZON.Texture>(copyingZON.Textures.Count);
        copyingZON.Textures.ForEach(delegate(ZON.Texture t) { temporaryZON.Textures.Add(new ZON.Texture() { Path = t.Path }); });
        temporaryZON.Tiles = new List<ZON.Tile>(copyingZON.Tiles.Count);
        copyingZON.Tiles.ForEach(delegate(ZON.Tile t)
        {
            temporaryZON.Tiles.Add(new ZON.Tile()
            {
                BaseID1 = t.BaseID1, BaseID2 = t.BaseID2, Offset1 = t.Offset1, Offset2 = t.Offset2,
                IsBlending = t.IsBlending, Rotation = t.Rotation, TileType = t.TileType
            });
        });
        temporaryZON.EconomyInfo = new ZON.Economy()
        {
            AreaName = "0", IsUnderground = 0, ButtonBGM = "button1", ButtonBack = "button2",
            CheckCount = 35, StandardPopulation = 500, StandardGrowthRate = 30,
            MetalConsumption = 10, StoneConsumption = 20, WoodConsumption = 20,
            LeatherConsumption = 10, ClothConsumption = 10, AlchemyConsumption = 10,
            ChemicalConsumption = 10, IndustrialConsumption = 10, MedicineConsumption = 30,
            FoodConsumption = 10
        };

        for (int y = 0; y < sizeY; y++)
        {
            for (int x = 0; x < sizeX; x++)
            {
                string stem = string.Format("{0}_{1}", y + 30, x + 30);
                Directory.CreateDirectory(Path.Combine(outDir, stem + @"\LIGHTMAP"));
                emptyLIT.Save(Path.Combine(outDir, stem + @"\LIGHTMAP\BUILDINGLIGHTMAPDATA.LIT"));
                emptyLIT.Save(Path.Combine(outDir, stem + @"\LIGHTMAP\OBJECTLIGHTMAPDATA.LIT"));

                emptyIFO.MapInfo.Width = 16;
                emptyIFO.MapInfo.Height = 16;
                emptyIFO.MapInfo.MapCellX = y + 30;
                emptyIFO.MapInfo.MapCellY = x + 30;
                emptyIFO.MapInfo.MapName = stem;

                emptyIFO.Save(Path.Combine(outDir, stem + ".IFO"));
                emptyTIL.Save(Path.Combine(outDir, stem + ".TIL"));
                emptyMOV.Save(Path.Combine(outDir, stem + ".MOV"));
                emptyHIM.Save(Path.Combine(outDir, stem + ".HIM"));
            }
        }
        temporaryZON.Save(Path.Combine(outDir, zoneFile));
        Console.WriteLine("oracle written to " + outDir);
        return 0;
    }
}
