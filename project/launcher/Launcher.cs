// Settings launcher. The starting layout came from Table-Tennis-Rockstar-PC
// (github.com/davidptrssvea/Table-Tennis-Rockstar-PC, MIT; see THIRD_PARTY_NOTICES.md).

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace Amped3Launcher
{
    public class Program
    {
        [STAThread]
        public static void Main()
        {
            if (Array.IndexOf(Environment.GetCommandLineArgs(), "--selftest") > 0)
            {
                Environment.Exit(SelfTest());
            }
            var app = new Application();
            app.Run(new MainWindow());
        }

        // --selftest: checks of the launcher's pure logic; prints the result and
        // exits with 0 (pass) or 1.
        private static int SelfTest()
        {
            int[,] cases = { { 720, 1 }, { 800, 1 }, { 1080, 1 }, { 1440, 2 }, { 1600, 2 }, { 2160, 3 }, { 2880, 3 }, { 600, 1 } };
            for (int i = 0; i < cases.GetLength(0); ++i)
            {
                int got = MainWindow.ChooseInternalScale(cases[i, 0]);
                if (got != cases[i, 1])
                {
                    Console.WriteLine("SELFTEST FAIL: ChooseInternalScale(" + cases[i, 0] + ") = " + got + ", expected " + cases[i, 1]);
                    return 1;
                }
            }
            Console.WriteLine("SELFTEST OK");
            return 0;
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

        // Match the display: the largest internal scale (the guest renders 720
        // lines) that fits the display height.
        internal static int ChooseInternalScale(int displayHeight)
        {
            for (int scale = 3; scale > 1; --scale)
            {
                if (720 * scale <= displayHeight) return scale;
            }
            return 1;
        }
        private static readonly string[] PostValues = { "fsr", "cas", "bilinear" };

        private readonly string distDir = AppDomain.CurrentDomain.BaseDirectory;
        private readonly string configFile;

        // "Night session" theme.
        private const string ThemeXaml = @"
<ResourceDictionary xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                    xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
  <Style x:Key='Pill' TargetType='RadioButton'>
    <Setter Property='Foreground' Value='#A9BBD0'/>
    <Setter Property='FontSize' Value='12.5'/>
    <Setter Property='FontWeight' Value='SemiBold'/>
    <Setter Property='Margin' Value='0,0,6,6'/>
    <Setter Property='Cursor' Value='Hand'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='RadioButton'>
          <Border x:Name='b' Background='#18253A' BorderBrush='#263852' BorderThickness='1' CornerRadius='6' Padding='13,7'>
            <ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/>
          </Border>
          <ControlTemplate.Triggers>
            <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='#5FE1F0'/></Trigger>
            <Trigger Property='IsChecked' Value='True'>
              <Setter TargetName='b' Property='Background' Value='#5FE1F0'/>
              <Setter TargetName='b' Property='BorderBrush' Value='#5FE1F0'/>
              <Setter Property='Foreground' Value='#0F1724'/>
            </Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style TargetType='CheckBox'>
    <Setter Property='Foreground' Value='#E6EEF6'/>
    <Setter Property='FontSize' Value='12.5'/>
    <Setter Property='Margin' Value='0,4,18,4'/>
    <Setter Property='Cursor' Value='Hand'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='CheckBox'>
          <StackPanel Orientation='Horizontal' Background='Transparent'>
            <Border x:Name='box' Width='16' Height='16' CornerRadius='4' Background='#18253A' BorderBrush='#263852' BorderThickness='1' VerticalAlignment='Center'>
              <Path x:Name='tick' Data='M3,8 L6.5,11.5 L13,4.5' Stroke='#0F1724' StrokeThickness='2' Visibility='Collapsed'/>
            </Border>
            <ContentPresenter Margin='8,0,0,0' VerticalAlignment='Center'/>
          </StackPanel>
          <ControlTemplate.Triggers>
            <Trigger Property='IsChecked' Value='True'>
              <Setter TargetName='box' Property='Background' Value='#5FE1F0'/>
              <Setter TargetName='box' Property='BorderBrush' Value='#5FE1F0'/>
              <Setter TargetName='tick' Property='Visibility' Value='Visible'/>
            </Trigger>
            <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='box' Property='BorderBrush' Value='#5FE1F0'/></Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style TargetType='TextBox'>
    <Setter Property='Background' Value='#18253A'/>
    <Setter Property='Foreground' Value='#E6EEF6'/>
    <Setter Property='BorderBrush' Value='#263852'/>
    <Setter Property='CaretBrush' Value='#5FE1F0'/>
    <Setter Property='FontSize' Value='12'/>
    <Setter Property='Height' Value='34'/>
    <Setter Property='Padding' Value='8,0'/>
    <Setter Property='VerticalContentAlignment' Value='Center'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='TextBox'>
          <Border x:Name='b' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1' CornerRadius='6'>
            <ScrollViewer x:Name='PART_ContentHost' Margin='{TemplateBinding Padding}' VerticalAlignment='Center'/>
          </Border>
          <ControlTemplate.Triggers>
            <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='#5FE1F0'/></Trigger>
            <Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.4'/></Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style TargetType='ComboBox'>
    <Setter Property='Foreground' Value='#E6EEF6'/>
    <Setter Property='FontSize' Value='12.5'/>
    <Setter Property='Height' Value='34'/>
    <Setter Property='Cursor' Value='Hand'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='ComboBox'>
          <Grid>
            <ToggleButton Focusable='False' ClickMode='Press'
                          IsChecked='{Binding IsDropDownOpen, Mode=TwoWay, RelativeSource={RelativeSource TemplatedParent}}'>
              <ToggleButton.Template>
                <ControlTemplate TargetType='ToggleButton'>
                  <Border x:Name='b' Background='#18253A' BorderBrush='#263852' BorderThickness='1' CornerRadius='6'>
                    <Path HorizontalAlignment='Right' VerticalAlignment='Center' Margin='0,0,12,0' Data='M0,0 L4,4 L8,0' Stroke='#7F97B3' StrokeThickness='1.5'/>
                  </Border>
                  <ControlTemplate.Triggers>
                    <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='#5FE1F0'/></Trigger>
                  </ControlTemplate.Triggers>
                </ControlTemplate>
              </ToggleButton.Template>
            </ToggleButton>
            <ContentPresenter IsHitTestVisible='False' Margin='12,0,30,0' VerticalAlignment='Center'
                              Content='{TemplateBinding SelectionBoxItem}'/>
            <Popup IsOpen='{TemplateBinding IsDropDownOpen}' Placement='Bottom' AllowsTransparency='True' Focusable='False'>
              <Border Background='#18253A' BorderBrush='#263852' BorderThickness='1' CornerRadius='6' Margin='0,4,0,0'
                      MinWidth='{TemplateBinding ActualWidth}' MaxHeight='320'>
                <ScrollViewer><ItemsPresenter/></ScrollViewer>
              </Border>
            </Popup>
          </Grid>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style TargetType='ComboBoxItem'>
    <Setter Property='Foreground' Value='#E6EEF6'/>
    <Setter Property='Padding' Value='12,7'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='ComboBoxItem'>
          <Border x:Name='b' Background='Transparent' Padding='{TemplateBinding Padding}'><ContentPresenter/></Border>
          <ControlTemplate.Triggers>
            <Trigger Property='IsSelected' Value='True'><Setter Property='Foreground' Value='#5FE1F0'/></Trigger>
            <Trigger Property='IsHighlighted' Value='True'><Setter TargetName='b' Property='Background' Value='#263852'/></Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style x:Key='Plain' TargetType='Button'>
    <Setter Property='Foreground' Value='#A9BBD0'/>
    <Setter Property='FontSize' Value='13'/>
    <Setter Property='FontWeight' Value='SemiBold'/>
    <Setter Property='Cursor' Value='Hand'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='Button'>
          <Border x:Name='b' Background='#1C2A40' BorderBrush='#263852' BorderThickness='1' CornerRadius='6' Padding='16,0'>
            <ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/>
          </Border>
          <ControlTemplate.Triggers>
            <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='BorderBrush' Value='#5FE1F0'/></Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
  <Style x:Key='Go' TargetType='Button'>
    <Setter Property='Foreground' Value='White'/>
    <Setter Property='Cursor' Value='Hand'/>
    <Setter Property='Template'>
      <Setter.Value>
        <ControlTemplate TargetType='Button'>
          <Border x:Name='b' Background='#FF7A29' CornerRadius='6' Padding='26,0'>
            <ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/>
          </Border>
          <ControlTemplate.Triggers>
            <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='b' Property='Background' Value='#FF8F4D'/></Trigger>
            <Trigger Property='IsEnabled' Value='False'><Setter TargetName='b' Property='Opacity' Value='0.5'/></Trigger>
          </ControlTemplate.Triggers>
        </ControlTemplate>
      </Setter.Value>
    </Setter>
  </Style>
</ResourceDictionary>";

        private static readonly Color Navy = Color.FromRgb(0x0f, 0x17, 0x24);
        private static readonly Color HeaderTop = Color.FromRgb(0x1c, 0x2e, 0x48);
        private static readonly Color Cyan = Color.FromRgb(0x5f, 0xe1, 0xf0);
        private static readonly Color Muted = Color.FromRgb(0x7f, 0x97, 0xb3);
        private static readonly Color TextLight = Color.FromRgb(0xe6, 0xee, 0xf6);
        private static readonly Color ErrorRed = Color.FromRgb(0xff, 0x6b, 0x6b);

        private FontFamily displayFont;
        private RadioButton rbNative;
        private CheckBox chkWideFill;

        public MainWindow()
        {
            configFile = Path.Combine(distDir, "settings.ini");
            Title = "ReAmped 3";
            Width = 560;
            SizeToContent = SizeToContent.Height;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            ResizeMode = ResizeMode.NoResize;
            Background = new SolidColorBrush(Navy);
            FontFamily = new FontFamily("Segoe UI");
            Resources = (ResourceDictionary)System.Windows.Markup.XamlReader.Parse(ThemeXaml);
            // Exo 2 from the fonts folder next to the launcher (falls back to Segoe UI).
            displayFont = new FontFamily(new Uri(Path.Combine(distDir, "fonts") + Path.DirectorySeparatorChar), "./#Exo 2, Segoe UI");

            BuildUI();
            LoadSettings();
        }

        private void BuildUI()
        {
            var root = new StackPanel();

            // Header: gradient with a faint, static snowfall speckle and the wordmark.
            var header = new Grid { Height = 108 };
            header.Children.Add(new Border { Background = new LinearGradientBrush(HeaderTop, Navy, 90) });
            header.Children.Add(new Border { Background = Speckle() });
            var wordmark = new StackPanel { Margin = new Thickness(26, 22, 26, 0) };
            var title = new TextBlock { FontFamily = displayFont, FontSize = 38, FontWeight = FontWeights.ExtraBold, FontStyle = FontStyles.Italic, Foreground = Brushes.White };
            title.Inlines.Add(new Run("Re"));
            title.Inlines.Add(new Run("Amped") { Foreground = new SolidColorBrush(Cyan) });
            title.Inlines.Add(new Run(" 3"));
            wordmark.Children.Add(title);
            wordmark.Children.Add(new TextBlock { Text = "PC EDITION", FontSize = 10.5, FontWeight = FontWeights.Bold, Foreground = new SolidColorBrush(Muted), Margin = new Thickness(2, 0, 0, 0) });
            header.Children.Add(wordmark);
            root.Children.Add(header);

            var body = new StackPanel { Margin = new Thickness(26, 8, 26, 22) };
            root.Children.Add(body);

            // Game folder (the extracted disc: the folder holding default.xex)
            body.Children.Add(Label("GAME FOLDER"));
            var pathGrid = new Grid();
            pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            pathGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            txtGamePath = new TextBox();
            txtGamePath.TextChanged += (s, e) => UpdateGameStatus();
            var btnBrowse = new Button { Content = "Browse", Height = 34, Margin = new Thickness(8, 0, 0, 0), Style = (Style)Resources["Plain"] };
            btnBrowse.Click += (s, e) => BrowseForGame();
            Grid.SetColumn(btnBrowse, 1);
            pathGrid.Children.Add(txtGamePath);
            pathGrid.Children.Add(btnBrowse);
            body.Children.Add(pathGrid);
            txtGameStatus = new TextBlock { FontSize = 11.5, Margin = new Thickness(2, 5, 0, 0), Foreground = new SolidColorBrush(ErrorRed) };
            body.Children.Add(txtGameStatus);

            // Resolution
            body.Children.Add(Label("RESOLUTION"));
            var resPanel = new WrapPanel();
            rb720 = Pill("720p");
            rb1080 = Pill("1080p");
            rb1440 = Pill("1440p");
            rb4k = Pill("4K");
            int dw, dh;
            DisplaySize(out dw, out dh);
            rbNative = Pill(dw > 0 ? string.Format("Match the display ({0}x{1})", dw, dh) : "Match the display");
            foreach (var rb in new[] { rb720, rb1080, rb1440, rb4k, rbNative }) resPanel.Children.Add(rb);
            body.Children.Add(resPanel);

            // Frame rate
            body.Children.Add(Label("FRAME RATE"));
            var fpsGrid = new Grid();
            fpsGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            fpsGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(90) });
            comboFps = new ComboBox();
            comboFps.Items.Add("60 FPS");
            int hz = DisplayRefreshHz();
            comboFps.Items.Add(hz > 0 ? string.Format("Auto: match the display ({0} Hz)", hz) : "Auto: match the display");
            comboFps.Items.Add("30 FPS (original)");
            comboFps.Items.Add("90 FPS");
            comboFps.Items.Add("100 FPS");
            comboFps.Items.Add("120 FPS");
            comboFps.Items.Add("144 FPS");
            comboFps.Items.Add("Unlimited");
            comboFps.Items.Add("Custom");
            txtCustomFps = new TextBox { Margin = new Thickness(8, 0, 0, 0), Text = "141", ToolTip = "Frame rate cap (e.g. 141 for a 144 Hz VRR display)" };
            comboFps.SelectionChanged += (s, e) => txtCustomFps.IsEnabled = comboFps.SelectedIndex == FpsValues.Length - 1;
            Grid.SetColumn(txtCustomFps, 1);
            fpsGrid.Children.Add(comboFps);
            fpsGrid.Children.Add(txtCustomFps);
            body.Children.Add(fpsGrid);
            body.Children.Add(new TextBlock
            {
                Text = "Game speed is right at every setting. Auto caps just under your display's refresh rate for G-Sync / FreeSync.",
                Foreground = new SolidColorBrush(Muted),
                FontSize = 11.5,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(2, 6, 0, 0)
            });

            // Toggles
            var toggles = new WrapPanel { Margin = new Thickness(0, 12, 0, 0) };
            chkFullscreen = new CheckBox { Content = "Fullscreen", IsChecked = true };
            chkTearing = new CheckBox { Content = "Allow tearing / G-Sync / FreeSync (above 60)", IsChecked = true };
            // Fill wide screens: the later widescreen option (hidden until it exists).
            chkWideFill = new CheckBox { Content = "Fill wide screens", Visibility = Visibility.Collapsed };
            toggles.Children.Add(chkFullscreen);
            toggles.Children.Add(chkTearing);
            toggles.Children.Add(chkWideFill);
            body.Children.Add(toggles);

            // Image
            body.Children.Add(Label("IMAGE"));
            var imgGrid = new Grid();
            imgGrid.ColumnDefinitions.Add(new ColumnDefinition());
            imgGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(10) });
            imgGrid.ColumnDefinitions.Add(new ColumnDefinition());
            comboAA = new ComboBox();
            comboAA.Items.Add("Anti-aliasing: none");
            comboAA.Items.Add("Anti-aliasing: FXAA");
            comboAA.Items.Add("Anti-aliasing: FXAA Extreme");
            comboPost = new ComboBox();
            comboPost.Items.Add("Scaling: AMD FSR 1");
            comboPost.Items.Add("Scaling: AMD CAS");
            comboPost.Items.Add("Scaling: bilinear");
            Grid.SetColumn(comboPost, 2);
            imgGrid.Children.Add(comboAA);
            imgGrid.Children.Add(comboPost);
            body.Children.Add(imgGrid);

            // Bottom bar: status, Quit (saves), RIDE.
            var bar = new Grid { Margin = new Thickness(0, 22, 0, 0) };
            bar.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            bar.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            txtStatus = new TextBlock { FontSize = 12, Foreground = new SolidColorBrush(Cyan), VerticalAlignment = VerticalAlignment.Center };
            var btnQuit = new Button { Content = "Quit", Height = 40, Margin = new Thickness(0, 0, 10, 0), Style = (Style)Resources["Plain"] };
            btnQuit.Click += (s, e) => { SaveSettings(); Close(); };
            var btnRide = new Button
            {
                Content = new TextBlock { Text = "RIDE", FontFamily = displayFont, FontSize = 20, FontWeight = FontWeights.ExtraBold, FontStyle = FontStyles.Italic },
                Height = 40,
                Style = (Style)Resources["Go"]
            };
            btnRide.Click += (s, e) => LaunchGame();
            Grid.SetColumn(btnQuit, 1);
            Grid.SetColumn(btnRide, 2);
            bar.Children.Add(txtStatus);
            bar.Children.Add(btnQuit);
            bar.Children.Add(btnRide);
            body.Children.Add(bar);

            Content = root;
        }

        // Faint, static snowfall dots on a 26 px tile.
        private static Brush Speckle()
        {
            var dots = new GeometryGroup();
            dots.Children.Add(new EllipseGeometry(new Point(5, 7), 0.9, 0.9));
            dots.Children.Add(new EllipseGeometry(new Point(17, 3), 0.6, 0.6));
            dots.Children.Add(new EllipseGeometry(new Point(12, 18), 0.7, 0.7));
            dots.Children.Add(new EllipseGeometry(new Point(23, 21), 0.9, 0.9));
            var drawing = new DrawingGroup();
            drawing.Children.Add(new GeometryDrawing(Brushes.Transparent, null, new RectangleGeometry(new Rect(0, 0, 26, 26))));
            drawing.Children.Add(new GeometryDrawing(new SolidColorBrush(Color.FromArgb(46, 255, 255, 255)), null, dots));
            return new DrawingBrush(drawing)
            {
                TileMode = TileMode.Tile,
                Viewport = new Rect(0, 0, 26, 26),
                ViewportUnits = BrushMappingMode.Absolute
            };
        }

        private static TextBlock Label(string text)
        {
            return new TextBlock
            {
                Text = text,
                FontSize = 10.5,
                FontWeight = FontWeights.Bold,
                Foreground = new SolidColorBrush(Muted),
                Margin = new Thickness(2, 16, 0, 6)
            };
        }

        private RadioButton Pill(string text)
        {
            return new RadioButton { Content = text, GroupName = "res", Style = (Style)Resources["Pill"] };
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

        // Shown only when something is wrong with the game folder.
        private void UpdateGameStatus()
        {
            if (txtGameStatus == null) return;
            string path = txtGamePath.Text.Trim();
            if (string.IsNullOrEmpty(path))
            {
                txtGameStatus.Text = "Pick your extracted Amped 3 folder (the one with default.xex).";
            }
            else if (!GameFolderValid(path))
            {
                txtGameStatus.Text = "No default.xex in this folder.";
            }
            else
            {
                txtGameStatus.Text = "";
            }
            txtGameStatus.Visibility = txtGameStatus.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
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

        // The primary display's current resolution, 0 x 0 if unknown.
        private static void DisplaySize(out int width, out int height)
        {
            var dm = new DEVMODE();
            dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
            const int ENUM_CURRENT_SETTINGS = -1;
            width = height = 0;
            if (EnumDisplaySettings(null, ENUM_CURRENT_SETTINGS, ref dm))
            {
                width = dm.dmPelsWidth;
                height = dm.dmPelsHeight;
            }
        }

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
            bool wideFill = false;

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
                        else if (key == "wide_fill") bool.TryParse(val, out wideFill);
                    }
                }
                catch { }
            }

            txtGamePath.Text = game;
            UpdateGameStatus();

            if (res == "720p") rb720.IsChecked = true;
            else if (res == "1080p") rb1080.IsChecked = true;
            else if (res == "1440p") rb1440.IsChecked = true;
            else if (res == "native") rbNative.IsChecked = true;
            else rb4k.IsChecked = true;
            chkWideFill.IsChecked = wideFill;

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
            if (rbNative.IsChecked == true) return "native";
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
                    "post=" + PostValues[Math.Max(0, comboPost.SelectedIndex)],
                    "wide_fill=" + (chkWideFill.IsChecked == true).ToString().ToLower()
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
                case "native":
                    int dw, dh;
                    DisplaySize(out dw, out dh);
                    if (dw > 0 && dh > 0)
                    {
                        width = dw;
                        height = dh;
                        scale = ChooseInternalScale(dh);
                    }
                    break;
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
