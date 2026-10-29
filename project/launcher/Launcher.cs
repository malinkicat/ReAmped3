// Settings launcher. The starting layout came from Table-Tennis-Rockstar-PC
// (github.com/davidptrssvea/Table-Tennis-Rockstar-PC, MIT; see THIRD_PARTY_NOTICES.md).

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Amped3Launcher
{
    public class Program
    {
        [STAThread]
        public static void Main()
        {
            var app = new Application();
            app.Run(new MainWindow());
        }
    }

    public class MainWindow : Window
    {
        private TextBox txtGamePath;
        private TextBlock txtGameStatus;
        private RadioButton rb720, rb1080, rb1440, rb4k;
        private CheckBox chkFullscreen, chkTearing;
        private ComboBox comboFps, comboAA, comboPost;
        private TextBox txtCustomFps;
        private TextBlock txtStatus;

        // Framerate values matching the comboFps items; 0 = unlimited.
        // -2 = auto (just under the display's refresh rate, for VRR);
        // -1 = custom (txtCustomFps, e.g. a cap just under a VRR display's maximum).
        private const int AutoFps = -2;
        private static readonly int[] FpsValues = { 60, AutoFps, 30, 90, 100, 120, 144, 0, -1 };
        private static readonly string[] AAValues = { "none", "fxaa", "fxaa_extreme" };
        private static readonly string[] PostValues = { "fsr", "cas", "bilinear" };

        private readonly string distDir = AppDomain.CurrentDomain.BaseDirectory;
        private readonly string configFile;

        public MainWindow()
        {
            configFile = Path.Combine(distDir, "settings.ini");
            Title = "Amped 3 - Settings & Launcher";
            Width = 600;
            SizeToContent = SizeToContent.Height;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            ResizeMode = ResizeMode.NoResize;
            Background = new SolidColorBrush(Color.FromRgb(18, 20, 26));

            BuildUI();
            LoadSettings();
        }

        private void BuildUI()
        {
            var root = new StackPanel { Margin = new Thickness(24) };

            root.Children.Add(new TextBlock
            {
                Text = "AMPED 3",
                FontSize = 22,
                FontWeight = FontWeights.Bold,
                Foreground = new SolidColorBrush(Color.FromRgb(255, 120, 40)),
                TextAlignment = TextAlignment.Center
            });
            root.Children.Add(new TextBlock
            {
                Text = "PC Port (ReXGlue SDK) - Configuration & Launcher",
                FontSize = 12,
                Foreground = new SolidColorBrush(Color.FromRgb(140, 150, 170)),
                TextAlignment = TextAlignment.Center,
                Margin = new Thickness(0, 4, 0, 16)
            });

            // Game folder (the extracted disc: the folder holding default.xex)
            root.Children.Add(SectionTitle("GAME FOLDER (EXTRACTED DISC, CONTAINS DEFAULT.XEX)"));
            var pathGrid = new Grid { Margin = new Thickness(0, 4, 0, 4) };
            pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(110) });
            txtGamePath = new TextBox
            {
                Height = 30,
                FontSize = 11,
                Background = new SolidColorBrush(Color.FromRgb(26, 30, 40)),
                Foreground = Brushes.White,
                BorderBrush = new SolidColorBrush(Color.FromRgb(60, 75, 100)),
                Padding = new Thickness(6, 4, 6, 4),
                VerticalContentAlignment = VerticalAlignment.Center
            };
            txtGamePath.TextChanged += (s, e) => UpdateGameStatus();
            var btnBrowse = new Button
            {
                Content = "Browse...",
                Height = 30,
                Margin = new Thickness(8, 0, 0, 0),
                Background = new SolidColorBrush(Color.FromRgb(45, 60, 85)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.SemiBold
            };
            btnBrowse.Click += (s, e) => BrowseForGame();
            Grid.SetColumn(btnBrowse, 1);
            pathGrid.Children.Add(txtGamePath);
            pathGrid.Children.Add(btnBrowse);
            root.Children.Add(pathGrid);
            txtGameStatus = new TextBlock { FontSize = 11, Margin = new Thickness(2, 0, 0, 8) };
            root.Children.Add(txtGameStatus);

            // Internal resolution
            root.Children.Add(SectionTitle("RESOLUTION (INTERNAL RENDERING)"));
            var resGrid = new Grid { Margin = new Thickness(0, 4, 0, 10) };
            resGrid.ColumnDefinitions.Add(new ColumnDefinition());
            resGrid.ColumnDefinitions.Add(new ColumnDefinition());
            resGrid.RowDefinitions.Add(new RowDefinition());
            resGrid.RowDefinitions.Add(new RowDefinition());
            rb720 = Radio("720p (1280x720) - 1x Native");
            rb1080 = Radio("1080p window, 720p render");
            rb1440 = Radio("1440p (2560x1440) - 2x");
            rb4k = Radio("4K (3840x2160) - 3x");
            Grid.SetColumn(rb1080, 1);
            Grid.SetRow(rb1440, 1);
            Grid.SetRow(rb4k, 1); Grid.SetColumn(rb4k, 1);
            resGrid.Children.Add(rb720);
            resGrid.Children.Add(rb1080);
            resGrid.Children.Add(rb1440);
            resGrid.Children.Add(rb4k);
            root.Children.Add(resGrid);

            // Frame rate
            root.Children.Add(SectionTitle("FRAME RATE"));
            comboFps = new ComboBox { Height = 26, Margin = new Thickness(0, 4, 0, 4) };
            comboFps.Items.Add("60 FPS (recommended)");
            int hz = DisplayRefreshHz();
            comboFps.Items.Add(hz > 0 ? string.Format("Auto: match the display ({0} Hz)", hz) : "Auto: match the display");
            comboFps.Items.Add("30 FPS (original)");
            comboFps.Items.Add("90 FPS (smooth high frame rate)");
            comboFps.Items.Add("100 FPS");
            comboFps.Items.Add("120 FPS (experimental)");
            comboFps.Items.Add("144 FPS (experimental)");
            comboFps.Items.Add("Unlimited (experimental)");
            comboFps.Items.Add("Custom");
            var fpsGrid = new Grid();
            fpsGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            fpsGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(110) });
            txtCustomFps = new TextBox { Height = 26, Margin = new Thickness(8, 4, 0, 4), Text = "141",
                                         VerticalContentAlignment = VerticalAlignment.Center,
                                         ToolTip = "Frame rate cap (e.g. 141 for a 144 Hz VRR display)" };
            comboFps.SelectionChanged += (s, e) => txtCustomFps.IsEnabled = comboFps.SelectedIndex == FpsValues.Length - 1;
            Grid.SetColumn(txtCustomFps, 1);
            fpsGrid.Children.Add(comboFps);
            fpsGrid.Children.Add(txtCustomFps);
            root.Children.Add(fpsGrid);
            root.Children.Add(new TextBlock
            {
                Text = "Game speed is correct at every setting. Above 60, GPU emulation tops out around 110-130 FPS depending on the scene, so 90 holds steady frame times; 120 and up vary with the scene. Known issue above 60: other riders' trick animations can look sped up.",
                Foreground = new SolidColorBrush(Color.FromRgb(170, 175, 190)),
                FontSize = 11,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 0, 0, 8)
            });
            chkTearing = Check("Allow tearing above 60 FPS (no vsync; smooth with G-Sync / FreeSync)", true);
            root.Children.Add(chkTearing);
            root.Children.Add(new TextBlock
            {
                Text = "Auto: with tearing allowed (G-Sync / FreeSync), caps 3 FPS under the display's refresh rate (144 Hz -> 141) so VRR stays engaged; without, runs at the refresh rate with vsync. Unlimited: no cap.",
                Foreground = new SolidColorBrush(Color.FromRgb(170, 175, 190)),
                FontSize = 11,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(0, 0, 0, 8)
            });

            // Display
            root.Children.Add(SectionTitle("DISPLAY"));
            chkFullscreen = Check("Fullscreen", true);
            root.Children.Add(chkFullscreen);
            var aaGrid = new Grid { Margin = new Thickness(0, 4, 0, 8) };
            aaGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(160) });
            aaGrid.ColumnDefinitions.Add(new ColumnDefinition());
            aaGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(32) });
            aaGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(32) });
            var lblAA = new TextBlock { Text = "Anti-aliasing:", Foreground = Brushes.White, VerticalAlignment = VerticalAlignment.Center };
            comboAA = new ComboBox { Height = 26 };
            comboAA.Items.Add("None");
            comboAA.Items.Add("FXAA");
            comboAA.Items.Add("FXAA Extreme");
            Grid.SetColumn(comboAA, 1);
            aaGrid.Children.Add(lblAA);
            aaGrid.Children.Add(comboAA);
            var lblPost = new TextBlock { Text = "Scaling / sharpening:", Foreground = Brushes.White, VerticalAlignment = VerticalAlignment.Center };
            comboPost = new ComboBox { Height = 26 };
            comboPost.Items.Add("AMD FSR 1 (best when the window is larger than the render)");
            comboPost.Items.Add("AMD CAS (sharpening)");
            comboPost.Items.Add("Bilinear");
            Grid.SetRow(lblPost, 1);
            Grid.SetRow(comboPost, 1); Grid.SetColumn(comboPost, 1);
            aaGrid.Children.Add(lblPost);
            aaGrid.Children.Add(comboPost);
            root.Children.Add(aaGrid);

            txtStatus = new TextBlock
            {
                FontSize = 12,
                Foreground = new SolidColorBrush(Color.FromRgb(70, 220, 130)),
                TextAlignment = TextAlignment.Center,
                Margin = new Thickness(0, 4, 0, 4)
            };
            root.Children.Add(txtStatus);

            var btnPanel = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 6, 0, 0) };
            var btnSave = FooterButton("Save Settings", 140, Color.FromRgb(40, 50, 70));
            btnSave.Click += (s, e) => { SaveSettings(); txtStatus.Text = "Settings saved to settings.ini"; };
            var btnCancel = FooterButton("Cancel", 90, Color.FromRgb(50, 55, 65));
            btnCancel.Click += (s, e) => Close();
            var btnLaunch = FooterButton("LAUNCH GAME", 170, Color.FromRgb(35, 134, 54));
            btnLaunch.FontWeight = FontWeights.Bold;
            btnLaunch.FontSize = 14;
            btnLaunch.Click += (s, e) => LaunchGame();
            btnPanel.Children.Add(btnSave);
            btnPanel.Children.Add(btnCancel);
            btnPanel.Children.Add(btnLaunch);
            root.Children.Add(btnPanel);

            Content = root;
        }

        private static TextBlock SectionTitle(string text)
        {
            return new TextBlock
            {
                Text = text,
                FontSize = 12,
                FontWeight = FontWeights.Bold,
                Foreground = new SolidColorBrush(Color.FromRgb(100, 180, 255)),
                Margin = new Thickness(0, 4, 0, 2)
            };
        }

        private static RadioButton Radio(string text)
        {
            return new RadioButton { Content = text, GroupName = "res", Foreground = Brushes.White, FontSize = 12, Margin = new Thickness(4, 3, 4, 3) };
        }

        private static CheckBox Check(string text, bool isChecked)
        {
            return new CheckBox { Content = text, Foreground = Brushes.White, FontSize = 13, Margin = new Thickness(4, 3, 0, 8), IsChecked = isChecked };
        }

        private static Button FooterButton(string text, double width, Color background)
        {
            return new Button
            {
                Content = text,
                Width = width,
                Height = 38,
                Margin = new Thickness(0, 0, 10, 0),
                Background = new SolidColorBrush(background),
                Foreground = Brushes.White,
                FontWeight = FontWeights.SemiBold
            };
        }

        private void BrowseForGame()
        {
            // WPF has no folder picker; pick default.xex and use its folder.
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Title = "Select default.xex in the extracted Amped 3 folder",
                Filter = "Xbox 360 executable (default.xex)|default.xex|All files (*.*)|*.*"
            };
            if (dlg.ShowDialog() == true)
            {
                txtGamePath.Text = Path.GetDirectoryName(dlg.FileName);
            }
        }

        private bool GameFolderValid(string path)
        {
            return !string.IsNullOrEmpty(path) && File.Exists(Path.Combine(path, "default.xex"));
        }

        private void UpdateGameStatus()
        {
            if (txtGameStatus == null) return;
            string path = txtGamePath.Text.Trim();
            if (string.IsNullOrEmpty(path))
            {
                txtGameStatus.Text = "Select the folder of your extracted Amped 3 disc.";
                txtGameStatus.Foreground = new SolidColorBrush(Color.FromRgb(255, 180, 80));
            }
            else if (GameFolderValid(path))
            {
                txtGameStatus.Text = "Found default.xex";
                txtGameStatus.Foreground = new SolidColorBrush(Color.FromRgb(70, 220, 130));
            }
            else
            {
                txtGameStatus.Text = "No default.xex in this folder.";
                txtGameStatus.Foreground = new SolidColorBrush(Color.FromRgb(255, 90, 90));
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
            public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
            public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public short dmLogPixels;
            public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
            public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2;
            public int dmPanningWidth, dmPanningHeight;
        }

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")]
        private static extern bool AllowSetForegroundWindow(int processId);

        // Windows only lets the foreground process give another one the focus,
        // and the game's window appears a few seconds after it starts. So the
        // launcher stays until that window exists, brings it to the front, and
        // then closes (after 20 s at the latest).
        private void HandOverFocus(Process game)
        {
            try { AllowSetForegroundWindow(game.Id); } catch { }
            txtStatus.Text = "Starting the game...";
            IsEnabled = false;
            DateTime deadline = DateTime.Now.AddSeconds(20);
            var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            timer.Tick += (s, e) =>
            {
                IntPtr window = IntPtr.Zero;
                try
                {
                    game.Refresh();
                    if (!game.HasExited) window = game.MainWindowHandle;
                }
                catch { }
                if (window != IntPtr.Zero || game.HasExited || DateTime.Now > deadline)
                {
                    timer.Stop();
                    if (window != IntPtr.Zero) SetForegroundWindow(window);
                    Close();
                }
            };
            timer.Start();
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool EnumDisplaySettings(string deviceName, int modeNum, ref DEVMODE devMode);

        // The primary display's current refresh rate in Hz, or 0 if unknown.
        private static int DisplayRefreshHz()
        {
            var dm = new DEVMODE();
            dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
            const int ENUM_CURRENT_SETTINGS = -1;
            if (!EnumDisplaySettings(null, ENUM_CURRENT_SETTINGS, ref dm)) return 0;
            return dm.dmDisplayFrequency > 1 ? dm.dmDisplayFrequency : 0;
        }

        // With tearing allowed (VRR), a cap 3 below the refresh rate keeps VRR
        // engaged (144 Hz -> 141); with vsync, the refresh rate itself.
        // 60 Hz displays (and unknown ones) get 60.
        private static int AutoFpsFor(int hz, bool vrr)
        {
            if (hz <= 61) return 60;
            return vrr ? hz - 3 : hz;
        }

        // The frame rate cap chosen (0 = unlimited); a custom one from 30 to 1000.
        private int SelectedFps()
        {
            int fps = FpsValues[Math.Max(0, comboFps.SelectedIndex)];
            if (fps == AutoFps) return AutoFpsFor(DisplayRefreshHz(), chkTearing.IsChecked == true);
            if (fps >= 0) return fps;
            int custom;
            if (!int.TryParse(txtCustomFps.Text.Trim(), out custom)) return 60;
            return Math.Min(1000, Math.Max(30, custom));
        }

        private void LoadSettings()
        {
            string game = "";
            string res = "4k";
            int framerate = 60;
            bool tearing = true;
            bool fullscreen = true;
            string aa = "none";
            string post = "fsr";

            if (File.Exists(configFile))
            {
                try
                {
                    foreach (var line in File.ReadAllLines(configFile))
                    {
                        var parts = line.Split(new char[] { '=' }, 2);
                        if (parts.Length != 2) continue;
                        string key = parts[0].Trim().ToLowerInvariant();
                        string val = parts[1].Trim();
                        if (key == "game_path") game = val;
                        else if (key == "resolution") res = val;
                        else if (key == "framerate")
                        {
                            if (val.ToLowerInvariant() == "auto") framerate = AutoFps;
                            else int.TryParse(val, out framerate);
                        }
                        else if (key == "allow_tearing") bool.TryParse(val, out tearing);
                        else if (key == "fullscreen") bool.TryParse(val, out fullscreen);
                        else if (key == "aa") aa = val;
                        else if (key == "post") post = val;
                    }
                }
                catch { }
            }

            txtGamePath.Text = game;
            UpdateGameStatus();

            if (res == "720p") rb720.IsChecked = true;
            else if (res == "1080p") rb1080.IsChecked = true;
            else if (res == "1440p") rb1440.IsChecked = true;
            else rb4k.IsChecked = true;

            int fpsIndex = Array.IndexOf(FpsValues, framerate);
            if (fpsIndex < 0)
            {
                fpsIndex = FpsValues.Length - 1;
                txtCustomFps.Text = framerate.ToString();
            }
            comboFps.SelectedIndex = fpsIndex;
            txtCustomFps.IsEnabled = fpsIndex == FpsValues.Length - 1;
            chkTearing.IsChecked = tearing;
            chkFullscreen.IsChecked = fullscreen;
            comboAA.SelectedIndex = Math.Max(0, Array.IndexOf(AAValues, aa));
            comboPost.SelectedIndex = Math.Max(0, Array.IndexOf(PostValues, post));
        }

        private string SelectedResolution()
        {
            if (rb720.IsChecked == true) return "720p";
            if (rb1080.IsChecked == true) return "1080p";
            if (rb1440.IsChecked == true) return "1440p";
            return "4k";
        }

        private void SaveSettings()
        {
            try
            {
                File.WriteAllLines(configFile, new string[]
                {
                    "game_path=" + txtGamePath.Text.Trim(),
                    "resolution=" + SelectedResolution(),
                    "framerate=" + (FpsValues[Math.Max(0, comboFps.SelectedIndex)] == AutoFps ? "auto" : SelectedFps().ToString()),
                    "allow_tearing=" + (chkTearing.IsChecked == true).ToString().ToLower(),
                    "fullscreen=" + (chkFullscreen.IsChecked == true).ToString().ToLower(),
                    "aa=" + AAValues[Math.Max(0, comboAA.SelectedIndex)],
                    "post=" + PostValues[Math.Max(0, comboPost.SelectedIndex)]
                });
            }
            catch { }
        }

        private void LaunchGame()
        {
            string game = txtGamePath.Text.Trim();
            if (!GameFolderValid(game))
            {
                MessageBox.Show("Select the extracted Amped 3 folder (the one containing default.xex) before launching.", "Game Folder Missing", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            SaveSettings();

            // The guest renders 1280x720; the scale multiplies the internal resolution.
            int width = 1280, height = 720, scale = 1;
            switch (SelectedResolution())
            {
                case "1080p": width = 1920; height = 1080; break;
                case "1440p": width = 2560; height = 1440; scale = 2; break;
                case "4k": width = 3840; height = 2160; scale = 3; break;
            }
            int fps = SelectedFps();
            bool highFps = fps == 0 || fps > 60;

            string exePath = Path.Combine(distDir, "amped3.exe");
            if (!File.Exists(exePath))
            {
                MessageBox.Show("amped3.exe is missing at: " + exePath, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            string args = string.Format(
                "--game_data_root=\"{0}\" --resolution_scale={1} --draw_resolution_scale_x={1} --draw_resolution_scale_y={1} --window_width={2} --window_height={3} --fullscreen={4} --swap_post_effect={5} --present_effect={6} --framerate={7} --mnk_mode=true --log_file=\"{8}\"",
                game, scale, width, height, (chkFullscreen.IsChecked == true).ToString().ToLower(),
                AAValues[Math.Max(0, comboAA.SelectedIndex)], PostValues[Math.Max(0, comboPost.SelectedIndex)], fps, Path.Combine(distDir, "amped3.log"));
            // Above 60 the game picks tearing on by default; only override when turned off.
            if (highFps && chkTearing.IsChecked != true)
            {
                args += " --vsync=true --d3d12_allow_variable_refresh_rate_and_tearing=false";
            }
            // The launcher's own arguments go to the game (play_diag.bat: frame-time logging).
            string[] launcherArgs = Environment.GetCommandLineArgs();
            for (int i = 1; i < launcherArgs.Length; ++i)
            {
                args += " " + launcherArgs[i];
            }

            try
            {
                Process gameProcess = Process.Start(new ProcessStartInfo { FileName = exePath, Arguments = args, WorkingDirectory = distDir, UseShellExecute = false });
                HandOverFocus(gameProcess);
            }
            catch (Exception ex)
            {
                MessageBox.Show("Could not start the game:\n" + ex.Message, "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
    }
}
