using System;
using System.IO;
using System.Threading;
using System.Windows.Threading;
using Map_Editor.Engine.Map;
using Map_Editor.Forms.Controls;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;
using Map_Editor.Engine.Data;

namespace Map_Editor.Engine
{
    /// <summary>
    /// MapManager class.
    /// </summary>
    public static class MapManager
    {
        #region Draw Order Constants

        /// <summary>
        /// Sky draw order.
        /// </summary>
        public const int DRAWORDER_SKY = 1;

        /// <summary>
        /// Heightmap draw order.
        /// </summary>
        public const int DRAWORDER_TERRAIN = 2;

        /// <summary>
        /// Decoration draw order.
        /// </summary>
        public const int DRAWORDER_DECORATION = 3;

        /// <summary>
        /// Constructions draw order.
        /// </summary>
        public const int DRAWORDER_CONSTRUCTION = 4;

        /// <summary>
        /// Event Trigger draw order.
        /// </summary>
        public const int DRAWORDER_EVENTRIGGERS = 5;

        /// <summary>
        /// NPC draw order.
        /// </summary>
        public const int DRAWORDER_NPC = 6;

        /// <summary>
        /// Monster draw order.
        /// </summary>
        public const int DRAWORDER_MONSTERS = 7;

        /// <summary>
        /// Spawn Point draw order.
        /// </summary>
        public const int DRAWORDER_SPAWNPOINTS = 8;

        /// <summary>
        /// Warp Gate draw order.
        /// </summary>
        public const int DRAWORDER_WARPGATES = 9;

        /// <summary>
        /// Sound draw order.
        /// </summary>
        public const int DRAWORDER_SOUNDS = 10;

        /// <summary>
        /// Effect draw order.
        /// </summary>
        public const int DRAWORDER_EFFECTS = 11;

        /// <summary>
        /// Water draw order.
        /// </summary>
        public const int DRAWORDER_WATER = 14;

        /// <summary>
        /// Animation draw order.
        /// </summary>
        public const int DRAWORDER_ANIMATION = 13;

        /// <summary>
        /// Collision draw order.
        /// </summary>
        public const int DRAWORDER_COLLISION = 12;

        #endregion

        #region Member Declarations

        /// <summary>
        /// Gets or sets the sky.
        /// </summary>
        /// <value>The sky.</value>
        public static Misc.Sky Sky { get; set; }

        /// <summary>
        /// Gets or sets the heightmaps.
        /// </summary>
        /// <value>The heightmaps.</value>
        public static Terrain.Heightmaps Heightmaps { get; set; }

        /// <summary>
        /// Gets or sets the decoration.
        /// </summary>
        /// <value>The decoration.</value>
        public static Objects.Decoration Decoration { get; set; }

        /// <summary>
        /// Gets or sets the construction.
        /// </summary>
        /// <value>The construction.</value>
        public static Objects.Construction Construction { get; set; }

        /// <summary>
        /// Gets or sets the event triggers.
        /// </summary>
        /// <value>The event triggers.</value>
        public static Objects.EventTriggers EventTriggers { get; set; }

        /// <summary>
        /// Gets or sets the NPCs.
        /// </summary>
        /// <value>The NPCs.</value>
        public static Characters.NPCs NPCs { get; set; }

        /// <summary>
        /// Gets or sets the monsters.
        /// </summary>
        /// <value>The monsters.</value>
        public static Characters.Monsters Monsters { get; set; }

        /// <summary>
        /// Gets or sets the spawn points.
        /// </summary>
        /// <value>The spawn points.</value>
        public static Events.SpawnPoints SpawnPoints { get; set; }

        /// <summary>
        /// Gets or sets the warp gates.
        /// </summary>
        /// <value>The warp gates.</value>
        public static Events.WarpGates WarpGates { get; set; }

        /// <summary>
        /// Gets or sets the sounds.
        /// </summary>
        /// <value>The sounds.</value>
        public static Misc.Sounds Sounds { get; set; }

        /// <summary>
        /// Gets or sets the effects.
        /// </summary>
        /// <value>The effects.</value>
        public static Misc.Effects Effects { get; set; }

        /// <summary>
        /// Gets or sets the collision.
        /// </summary>
        /// <value>The collision.</value>
        public static Objects.Collision Collision { get; set; }

        /// <summary>
        /// Gets or sets the water.
        /// </summary>
        /// <value>The water.</value>
        public static Terrain.Water Water { get; set; }

        /// <summary>
        /// Gets or sets the animation.
        /// </summary>
        /// <value>The animation.</value>
        public static Objects.Animation Animation { get; set; }

        /// <summary>
        /// Gets or sets the ID.
        /// </summary>
        /// <value>The ID.</value>
        public static int ID { get; private set; }

        /// <summary>
        /// Gets or sets the name.
        /// </summary>
        /// <value>The name.</value>
        public static string Name { get; private set; }

        #endregion

        /// <summary>
        /// Initializes this instance.
        /// </summary>
        /// <param name="game">The game.</param>
        public static void Initialize(Game game)
        {
            game.Components.Add(Sky = new Misc.Sky(game));
            game.Components.Add(Heightmaps = new Terrain.Heightmaps(game));
            game.Components.Add(Decoration = new Objects.Decoration(game));
            game.Components.Add(Construction = new Objects.Construction(game));
            game.Components.Add(EventTriggers = new Objects.EventTriggers(game));
            game.Components.Add(NPCs = new Characters.NPCs(game));
            game.Components.Add(Monsters = new Characters.Monsters(game));
            game.Components.Add(SpawnPoints = new Events.SpawnPoints(game));
            game.Components.Add(WarpGates = new Events.WarpGates(game));
            game.Components.Add(Sounds = new Misc.Sounds(game));
            game.Components.Add(Effects = new Misc.Effects(game));
            game.Components.Add(Collision = new Objects.Collision(game));
            game.Components.Add(Water = new Terrain.Water(game));
            game.Components.Add(Animation = new Objects.Animation(game));
        }

        /// <summary>
        /// Loads the specified map ID.
        /// </summary>
        /// <param name="mapID">The map ID.</param>
        public static void Load(int mapID)
        {
            DateTime loadStart = DateTime.Now;

            App.Form.ResetForm();
            App.Form.Freeze();

            ID = mapID;

            string mapFolder = Path.GetDirectoryName(FileManager.STBs["LIST_ZONE"].Cells[mapID][2]);

            App.Form.Title = string.Format("Map Editor - {0}", Name = FileManager.STLs["LIST_ZONE_S"].Search(FileManager.STBs["LIST_ZONE"].Cells[mapID][27]));

            Output.WriteLine(Output.MessageType.Normal, "--------------------------");
            Output.WriteLine(Output.MessageType.Event, string.Format("Loading Map {0}", Name));

            DecorationTool.ClearImages();
            ConstructionTool.ClearImages();
            AnimationTool.ClearImages();
            EventTriggerTool.ClearImages();

            Sky.Loading = true;
            Heightmaps.Loading = true;
            Decoration.Loading = true;
            Construction.Loading = true;
            EventTriggers.Loading = true;
            NPCs.Loading = true;
            Monsters.Loading = true;
            SpawnPoints.Loading = true;
            WarpGates.Loading = true;
            Sounds.Loading = true;
            Effects.Loading = true;
            Collision.Loading = true;
            Water.Loading = true;
            Animation.Loading = true;

            new Thread(new ThreadStart(delegate
            {
                string loadStep = "Starting map load";

                try
                {
                loadStep = "Resetting files";
                FileManager.Reset(false);

                #region Loading Zone Data

                loadStep = "Loading Zone Data";
                Output.WriteLine(Output.MessageType.Normal, "- Loading Zone Data");
                FileManager.Add(FileManager.STBs["LIST_ZONE"].Cells[mapID][2], FileManager.FileType.ZON);

                SpawnPoints.Clear();

                FileManager.ZON.SpawnPoints.ForEach(delegate(ZON.SpawnPoint spawnPoint)
                {
                    SpawnPoints.Add(spawnPoint);
                });

                #endregion

                #region Loading Sky

                loadStep = "Loading Sky";
                Output.WriteLine(Output.MessageType.Normal, "- Loading Sky");
                Sky.Load(FileManager.STBs["LIST_SKY"].Cells[Convert.ToInt32(FileManager.STBs["LIST_ZONE"].Cells[mapID][8])][1], FileManager.STBs["LIST_SKY"].Cells[Convert.ToInt32(FileManager.STBs["LIST_ZONE"].Cells[mapID][8])][2]);

                #endregion

                #region Loading Heightmaps

                loadStep = "Loading Heightmaps";
                Output.WriteLine(Output.MessageType.Normal, "- Loading Heightmaps");
                string[] himFiles = Directory.GetFiles(mapFolder, "*.HIM");

                Heightmaps.Clear();

                for (int i = 0; i < himFiles.Length; i++)
                    Heightmaps.Add(himFiles[i]);

                #endregion
                
                loadStep = "Loading Tile Set Data";
                Output.WriteLine(Output.MessageType.Normal, "- Loading Tile Set Data");

                string tileSetFile = GetTileSetFile(mapFolder);
                string tileSetPath = ResolveTileSetPath(tileSetFile);

                if (tileSetPath == null)
                {
                    Output.WriteLine(Output.MessageType.Error, string.Format("Missing Tile Set Data: {0}", tileSetFile));
                    FileManager.TileSet = new TileSet();
                }
                else
                {
                    FileManager.TileSet = new TileSet(tileSetPath);
                }

                loadStep = "Loading Map Data";
                Output.WriteLine(Output.MessageType.Normal, "- Loading Map Data");

                FileManager.AddFolder(mapFolder, FileManager.FileType.IFO);

                #region Loading Objects

                Decoration.Clear();
                Construction.Clear();
                EventTriggers.Clear();
                NPCs.Clear();
                Monsters.Clear();
                WarpGates.Clear();
                Sounds.Clear();
                Effects.Clear();
                Collision.Clear();
                Water.Clear();
                Animation.Clear();

                Output.WriteLine(Output.MessageType.Normal, "- Loading Map Objects");

                loadStep = "Loading Decoration ZSC";
                FileManager.Add("Decoration", FileManager.STBs["LIST_ZONE"].Cells[mapID][12], FileManager.FileType.ZSC);
                loadStep = "Loading Construction ZSC";
                FileManager.Add("Construction", FileManager.STBs["LIST_ZONE"].Cells[mapID][13], FileManager.FileType.ZSC);

                for(int i = 0; i < FileManager.IFOs.Count; i++)
                {
                    loadStep = string.Format("Loading IFO {0}", FileManager.IFOs[i].FileName);
                    string subFolder = FileManager.IFOs[i].FileName.Substring(0, 5);

                    #region Loading Lightmaps

                    FileManager.Add(string.Format(@"{0}\{1}\LIGHTMAP\OBJECTLIGHTMAPDATA.LIT", mapFolder, subFolder), FileManager.LITType.Decoration);
                    FileManager.Add(string.Format(@"{0}\{1}\LIGHTMAP\BUILDINGLIGHTMAPDATA.LIT", mapFolder, subFolder), FileManager.LITType.Construction);

                    #endregion

                    #region Loading Decoration

                    for (int j = 0; j < FileManager.IFOs[i].Decoration.Count; j++)
                        Decoration.Add(FileManager.IFOs[i].Decoration[j], i, j);

                    #endregion

                    #region Loading Construction

                    for (int j = 0; j < FileManager.IFOs[i].Construction.Count; j++)
                        Construction.Add(FileManager.IFOs[i].Construction[j], i, j);

                    #endregion

                    #region Loading Events

                    for (int j = 0; j < FileManager.IFOs[i].EventTriggers.Count; j++)
                        EventTriggers.Add(FileManager.IFOs[i].EventTriggers[j]);

                    #endregion

                    #region Loading NPCs

                    for (int j = 0; j < FileManager.IFOs[i].NPCs.Count; j++)
                        NPCs.Add(FileManager.IFOs[i].NPCs[j]);

                    #endregion

                    #region Loading Monsters

                    for (int j = 0; j < FileManager.IFOs[i].Monsters.Count; j++)
                        Monsters.Add(FileManager.IFOs[i].Monsters[j]);

                    #endregion

                    #region Loading Warp Gates

                    for (int j = 0; j < FileManager.IFOs[i].WarpGates.Count; j++)
                        WarpGates.Add(FileManager.IFOs[i].WarpGates[j]);

                    #endregion                    

                    #region Loading Sounds

                    for (int j = 0; j < FileManager.IFOs[i].Sounds.Count; j++)
                        Sounds.Add(FileManager.IFOs[i].Sounds[j]);

                    #endregion

                    #region Loading Effects

                    for (int j = 0; j < FileManager.IFOs[i].Effects.Count; j++)
                        Effects.Add(FileManager.IFOs[i].Effects[j]);

                    #endregion

                    #region Loading Collision

                    for (int j = 0; j < FileManager.IFOs[i].Collision.Count; j++)
                        Collision.Add(FileManager.IFOs[i].Collision[j]);

                    #endregion

                    #region Loading Water

                    for (int j = 0; j < FileManager.IFOs[i].Water.Count; j++)
                        Water.Add(FileManager.IFOs[i].Water[j]);

                    #endregion                    

                    #region Loading Animation

                    for (int j = 0; j < FileManager.IFOs[i].Animation.Count; j++)
                        Animation.Add(FileManager.IFOs[i].Animation[j]);

                    #endregion
                }

                Decoration.ObjectManager.Sort();
                Construction.ObjectManager.Sort();
                EventTriggers.ObjectManager.Sort();
                NPCs.ObjectManager.Sort();
                Monsters.ObjectManager.Sort();
                Animation.AnimationManager.Sort();

                #endregion

                try
                {
                    CameraManager.SetPosition(FileManager.ZON.SpawnPoints.Find(delegate(ZON.SpawnPoint spawnPoint)
                    {
                        return spawnPoint.Name == "start";
                    }).Position + new Vector3(0.0f, 0.0f, 5.0f));
                }
                catch
                {
                    CameraManager.SetPosition(new Vector3(5200.0f, 5200.0f, 0.0f));
                }

                Sky.Loading = false;
                Heightmaps.Loading = false;
                Decoration.Loading = false;
                Construction.Loading = false;
                EventTriggers.Loading = false;
                NPCs.Loading = false;
                Monsters.Loading = false;
                SpawnPoints.Loading = false;
                WarpGates.Loading = false;
                Sounds.Loading = false;
                Effects.Loading = false;
                Collision.Loading = false;
                Water.Loading = false;
                Animation.Loading = false;

                App.Form.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (DispatcherOperationCallback)delegate
                {
                    App.Form.UnFreeze();

                    return null;

                }, null);

                Output.WriteLine(Output.MessageType.Event, string.Format("Loading Completed in {0} Second(s)", (DateTime.Now - loadStart).TotalSeconds));
                }
                catch (Exception ex)
                {
                    Output.WriteException(string.Format("Map load failed during {0}", loadStep), ex);

                    Sky.Loading = false;
                    Heightmaps.Loading = false;
                    Decoration.Loading = false;
                    Construction.Loading = false;
                    EventTriggers.Loading = false;
                    NPCs.Loading = false;
                    Monsters.Loading = false;
                    SpawnPoints.Loading = false;
                    WarpGates.Loading = false;
                    Sounds.Loading = false;
                    Effects.Loading = false;
                    Collision.Loading = false;
                    Water.Loading = false;
                    Animation.Loading = false;

                    App.Form.Dispatcher.BeginInvoke(DispatcherPriority.Normal, (DispatcherOperationCallback)delegate
                    {
                        App.Form.UnFreeze();
                        System.Windows.MessageBox.Show(string.Format("Map load failed during {0}.\n\nDetails were written to Map Editor.log.\n\n{1}", loadStep, ex.Message), "Map Editor Error", System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);

                        return null;

                    }, null);
                }
                
            }))
            {
                IsBackground = true,
                ApartmentState = ApartmentState.STA
            }.Start();
        }

        /// <summary>
        /// Resolves editor tile set data across legacy and Rose Next data layouts.
        /// </summary>
        /// <param name="tileSetFile">The tile set file name from ZONETYPEINFO.STB.</param>
        /// <returns>The first existing tile set path, or null if none exists.</returns>
        private static string ResolveTileSetPath(string tileSetFile)
        {
            if (tileSetFile == null || tileSetFile.Trim().Length == 0)
                return null;

            string[] candidates = new string[]
            {
                Path.Combine("3Ddata\\ESTB", tileSetFile),
                Path.Combine("ESTB", tileSetFile)
            };

            for (int i = 0; i < candidates.Length; i++)
            {
                if (File.Exists(candidates[i]))
                    return candidates[i];
            }

            return null;
        }

        /// <summary>
        /// Gets the tile set helper file for the current map.
        /// </summary>
        /// <param name="mapFolder">The map folder path.</param>
        /// <returns>The tile set file name.</returns>
        private static string GetTileSetFile(string mapFolder)
        {
            int zoneType = FileManager.ZON.ZoneInfo.ZoneType;

            if (zoneType >= 0 && zoneType < FileManager.STBs["ZONETYPEINFO"].Cells.Count)
                return FileManager.STBs["ZONETYPEINFO"].Cells[zoneType][6];

            string inferredTileSetFile = InferTileSetFile(mapFolder);

            if (inferredTileSetFile != null)
            {
                Output.WriteLine(Output.MessageType.Error, string.Format("Unknown ZoneType {0}; using inferred tile set {1}", zoneType, inferredTileSetFile));
                return inferredTileSetFile;
            }

            Output.WriteLine(Output.MessageType.Error, string.Format("Unknown ZoneType {0}; using empty tile set", zoneType));
            return null;
        }

        /// <summary>
        /// Infers a tile set file from the map folder for incomplete zones.
        /// </summary>
        /// <param name="mapFolder">The map folder path.</param>
        /// <returns>The inferred tile set file name.</returns>
        private static string InferTileSetFile(string mapFolder)
        {
            string folder = mapFolder.ToUpperInvariant();
            string mapName = Path.GetFileName(mapFolder).ToUpperInvariant();

            if (folder.Contains("\\ELDEON\\EZ") || mapName.StartsWith("EZ"))
                return "Table_Tileset_EZ.STB";

            if (folder.Contains("\\ELDEON\\EV") || mapName.StartsWith("EV"))
                return "Table_Tileset_EV.STB";

            if (folder.Contains("\\ELDEON\\EJ") || mapName.StartsWith("EJ"))
                return "Table_Tileset_EJ.STB";

            if (folder.Contains("\\LUNAR\\LZ") || mapName.StartsWith("LZ"))
                return "Table_Tileset_LZ.STB";

            if (folder.Contains("\\LUNAR\\LP") || mapName.StartsWith("LP"))
                return "Table_Tileset_LP.STB";

            if (folder.Contains("\\JUNON\\JDT") || mapName.StartsWith("JDT"))
                return "Table_Tileset_JDT.STB";

            if (folder.Contains("\\JUNON\\JPTBG") || mapName.StartsWith("JPTBG"))
                return "Table_Tileset_JPTBG.STB";

            if (folder.Contains("\\JUNON\\JPT") || mapName.StartsWith("JPT"))
                return "Table_Tileset_JPT.STB";

            if (folder.Contains("\\JUNON\\JZP") || mapName.StartsWith("JZP"))
                return "Table_Tileset_JZP.STB";

            if (folder.Contains("\\JUNON\\JZ") || mapName.StartsWith("JZ"))
                return "Table_Tileset_JZ.STB";

            if (folder.Contains("\\JUNON\\JD") || mapName.StartsWith("JD"))
                return "Table_Tileset_JD.STB";

            if (folder.Contains("\\JUNON\\JG") || mapName.StartsWith("JG"))
                return "Table_Tileset_JG.STB";

            return null;
        }

        /// <summary>
        /// Determines whether [is valid map] [the specified id].
        /// </summary>
        /// <param name="id">The id.</param>
        /// <returns>
        /// 	<c>true</c> if [is valid map] [the specified id]; otherwise, <c>false</c>.
        /// </returns>
        public static bool IsValidMap(int id)
        {
            // Missing ZON Path
            if (FileManager.STBs["LIST_ZONE"].Cells[id][2].Trim().Length == 0)
                return false;

            // Missing Start Spawn
            if (FileManager.STBs["LIST_ZONE"].Cells[id][3].Trim().Length == 0)
                return false;

            // Missing Restore Spawn
            if (FileManager.STBs["LIST_ZONE"].Cells[id][4].Trim().Length == 0)
                return false;

            // Missing Sky Setting
            if (FileManager.STBs["LIST_ZONE"].Cells[id][8].Trim().Length == 0)
                return false;

            // Missing Y IFO
            if (FileManager.STBs["LIST_ZONE"].Cells[id][10].Trim().Length == 0)
                return false;

            // Missing X IFO
            if (FileManager.STBs["LIST_ZONE"].Cells[id][11].Trim().Length == 0)
                return false;

            // Missing Decoration ZSC
            if (FileManager.STBs["LIST_ZONE"].Cells[id][12].Trim().Length == 0)
                return false;

            // Missing Construction ZSC
            if (FileManager.STBs["LIST_ZONE"].Cells[id][13].Trim().Length == 0)
                return false;

            return true;
        }

        /// <summary>
        /// Gets the file index of the specified position.
        /// </summary>
        /// <param name="position">The position.</param>
        /// <returns>The file index of the specified position.</returns>
        public static int InsideFile(Vector3 position)
        {
            Vector2 fileBlock = new Vector2((int)Math.Floor(-(((position.Y - 10400.0f) + 1) / 160.0f)), (int)Math.Floor(position.X / 160.0f));

            for (int i = 0; i < FileManager.IFOs.Count; i++)
            {
                if (string.Compare(FileManager.IFOs[i].FileName, string.Format("{0}_{1}.IFO", fileBlock.Y, fileBlock.X), true) == 0)
                    return i;
            }

            throw new Exception("IFO block does not exist");
        }

        /// <summary>
        /// Get the block offset of the specified position.
        /// </summary>
        /// <param name="position">The position.</param>
        /// <returns>The block offset of the specified position.</returns>
        public static Vector2 InsideBlock(Vector3 position)
        {
            return new Vector2((int)Math.Floor(-(((position.Y - 10400.0f) + 1) / 160.0f)), (int)Math.Floor(position.X / 160.0f));
        }

        /// <summary>
        /// Gets the map position.
        /// </summary>
        /// <param name="block">The block.</param>
        /// <param name="position">The position.</param>
        /// <returns>The 64x64 map position.</returns>
        public static Vector2 MapPosition(Vector2 block, Vector3 position)
        {
            Vector2 mapPosition = new Vector2(block.Y * 160.0f, 10400.0f - ((block.X + 1) * 160.0f));

            return new Vector2((int)((position.Y - mapPosition.X) / 4), (int)((position.X - mapPosition.Y) / 4));
        }
    }
}
