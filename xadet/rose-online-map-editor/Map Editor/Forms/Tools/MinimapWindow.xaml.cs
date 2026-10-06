using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Map_Editor.Engine;
using Map_Editor.Engine.Data;
using Map_Editor.Engine.Minimap;
using Map_Editor.Misc;
using Microsoft.Xna.Framework;

namespace Map_Editor.Forms.Tools
{
    /// <summary>
    /// Tools > Make minimap: renders the open map from straight above (unsaved
    /// edits included), styles it like a retail minimap, shows it beside the
    /// zone's current one, and writes it as the zone's minimap on Install.
    /// </summary>
    public partial class MinimapWindow : Window
    {
        private MinimapZone zone;
        private MinimapRender render;
        private MinimapResult result;
        private byte[] original;
        private bool busy;

        public MinimapWindow()
        {
            InitializeComponent();
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                zone = MinimapZone.ForZone(MapManager.ID);
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: cannot place the minimap of this zone", ex);
                MessageBox.Show(this, "This zone's minimap cannot be placed: " + ex.Message, Title, MessageBoxButton.OK, MessageBoxImage.Error);
                Close();
                return;
            }
            RefreshCurrent();
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            if (busy)
            {
                e.Cancel = true;
                Status.Text = "Wait for the minimap to finish.";
            }
        }

        /// <summary>The zone's line, the right-hand picture and the buttons.</summary>
        private void RefreshCurrent()
        {
            original = MinimapInputs.Original(zone);

            byte[] current = null;
            try
            {
                current = zone.ReadCurrent();
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: cannot read " + zone.MinimapPath, ex);
            }
            CurrentImage.Source = current != null ? ToImage(current, zone.Width, zone.Height) : null;
            CurrentHint.Visibility = current != null ? Visibility.Collapsed : Visibility.Visible;
            CurrentTitle.Text = zone.IsInstalled ? "Current minimap (installed from here)" : "Current minimap";
            MatchBox.IsEnabled = original != null;

            Info.Text = string.Format("{0} (zone {1}, {2}): {3} x {4} px, top-left chunk {5}_{6}, 2.5 m a pixel. {7}",
                                      MapManager.Name, zone.ID, zone.Folder, zone.Width, zone.Height, zone.StartX, zone.StartY,
                                      zone.HasMinimap ? "Same extent as the current minimap, so the two line up."
                                                      : "No minimap yet: Install creates one and fills in LIST_ZONE.");
            UpdateButtons();
        }

        private void UpdateButtons()
        {
            GenerateButton.IsEnabled = !busy;
            SaveButton.IsEnabled = !busy && result != null;
            InstallButton.IsEnabled = !busy && result != null;
            RestoreButton.IsEnabled = !busy && zone != null && zone.IsInstalled;
            MatchBox.IsEnabled = !busy && original != null;
            LabelsBox.IsEnabled = WavesBox.IsEnabled = QualityBox.IsEnabled = !busy;
        }

        private void Generate_Click(object sender, RoutedEventArgs e)
        {
            if (MapManager.Heightmaps == null || MapManager.Heightmaps.Loading)
            {
                Status.Text = "The map is still loading.";
                return;
            }

            App.Form.ReleaseTool();
            busy = true;
            UpdateButtons();
            Status.Text = "Drawing the map from above...";
            MinimapCapture.Start(zone, QualityBox.SelectedIndex == 0 ? 2 : 4,
                delegate(MinimapRender r)
                {
                    render = r;
                    busy = false;
                    Restyle();
                },
                delegate(string error)
                {
                    busy = false;
                    UpdateButtons();
                    Status.Text = "Drawing failed: " + error;
                });
        }

        private void Option_Click(object sender, RoutedEventArgs e)
        {
            if (render != null && !busy)
                Restyle();
        }

        /// <summary>Styles the last render with the current options, off the UI thread.</summary>
        private void Restyle()
        {
            MinimapStyleInput input = MinimapInputs.Gather(zone, render, original, MatchBox.IsChecked == true,
                                                           LabelsBox.IsChecked == true, WavesBox.IsChecked == true);

            busy = true;
            UpdateButtons();
            Status.Text = "Styling...";
            new Thread(delegate()
            {
                MinimapResult made = null;
                Exception error = null;
                try
                {
                    made = MinimapStyle.Make(input);
                }
                catch (Exception ex)
                {
                    error = ex;
                }
                Dispatcher.BeginInvoke(new Action(delegate()
                {
                    busy = false;
                    if (error != null)
                    {
                        Output.WriteException("Minimap: styling failed", error);
                        Status.Text = "Styling failed: " + error.Message;
                    }
                    else
                    {
                        result = made;
                        NewImage.Source = ToImage(made.Rgb, made.Width, made.Height);
                        NewHint.Visibility = Visibility.Collapsed;
                        string diff = made.Difference >= 0 ? string.Format("Difference to the original: {0:0.0}. ", made.Difference) : "";
                        Status.Text = diff + string.Join("; ", made.Notes.ToArray());
                    }
                    UpdateButtons();
                }));
            }) { IsBackground = true }.Start();
        }

        private void Save_Click(object sender, RoutedEventArgs e)
        {
            Microsoft.Win32.SaveFileDialog dialog = new Microsoft.Win32.SaveFileDialog
            {
                Filter = "PNG image (*.png)|*.png",
                FileName = zone.Folder.ToLowerInvariant() + "_minimap.png"
            };
            if (dialog.ShowDialog(this) != true)
                return;

            using (System.Drawing.Bitmap bmp = MinimapStyle.ToBitmap(result.Rgb, result.Width, result.Height))
                bmp.Save(dialog.FileName, System.Drawing.Imaging.ImageFormat.Png);
            Status.Text = "Saved " + dialog.FileName;
        }

        private void Install_Click(object sender, RoutedEventArgs e)
        {
            string problem = zone.InstallProblem();
            if (problem != null)
            {
                MessageBox.Show(this, "Cannot install: " + problem + ".", Title, MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            string question = string.Format("Write the new minimap as {0}?{1}\n\nThe file it replaces is kept in {2}; Restore original puts it back.",
                                             zone.TargetPath,
                                             zone.HasMinimap ? "" : "\n\nThis zone has no minimap: LIST_ZONE.STB gets its file name and top-left chunk.",
                                             Path.GetFullPath(zone.BackupDirectory));
            if (MessageBox.Show(this, question, Title, MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
                return;

            try
            {
                zone.Install(result.Rgb);
                Output.WriteLine(Output.MessageType.Event, "Minimap: installed " + zone.TargetPath);
                RefreshCurrent();
                Status.Text = "Installed. The game reads it from its packed data: re-pack, then restart the client.";
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: install failed", ex);
                MessageBox.Show(this, "Install failed: " + ex.Message, Title, MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void Restore_Click(object sender, RoutedEventArgs e)
        {
            if (MessageBox.Show(this, "Put back the minimap this zone had before the first Install?", Title,
                                MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
                return;

            try
            {
                zone.Restore();
                zone = MinimapZone.ForZone(zone.ID);
                Output.WriteLine(Output.MessageType.Event, "Minimap: restored the original of " + zone.Folder);
                RefreshCurrent();
                Status.Text = "Original restored.";
            }
            catch (Exception ex)
            {
                Output.WriteException("Minimap: restore failed", ex);
                MessageBox.Show(this, "Restore failed: " + ex.Message, Title, MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void Close_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private static BitmapSource ToImage(byte[] rgb, int width, int height)
        {
            BitmapSource image = BitmapSource.Create(width, height, 96, 96, PixelFormats.Rgb24, null, rgb, width * 3);
            image.Freeze();
            return image;
        }
    }
}
