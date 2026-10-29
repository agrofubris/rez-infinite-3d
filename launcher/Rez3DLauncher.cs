// Rez Infinite 3D launcher: edits rez3d.ini and starts the game in stereo.
// Styled after Rez's own menus: black, lowercase spaced type, orange items,
// the selected item in white. Keyboard (arrows/enter/esc) or mouse (click,
// wheel). Settings are written to the ini on every change.
//
// Build (C# 5, ships with Windows):
//   %WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe /target:winexe
//       /out:Rez3D.exe Rez3DLauncher.cs

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Globalization;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Rez3D
{
    enum Kind { Action, Number, Choice }

    class Item
    {
        public string Label, Key, Help;
        public Kind Kind;
        public double Min, Max, Step, Default;
        public string Unit = "";
        public int Decimals;
        public string[] Choices, ChoiceLabels;
        public string DefaultChoice;
        public Action Run;
        public string Value;  // text shown in the value column of an action (setup: the game folder)

        public double Num;
        public int ChoiceIndex;
    }

    class Ini
    {
        public static Dictionary<string, string> Read(string path)
        {
            var kv = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (!File.Exists(path)) return kv;
            foreach (var raw in File.ReadAllLines(path))
            {
                var s = raw.Trim();
                if (s.Length == 0 || s[0] == ';' || s[0] == '#' || s[0] == '[') continue;
                int eq = s.IndexOf('=');
                if (eq <= 0) continue;
                kv[s.Substring(0, eq).Trim()] = s.Substring(eq + 1).Trim();
            }
            return kv;
        }

        // Replaces existing key lines in place (keeping comments and order) and
        // appends keys that are missing.
        public static void Write(string path, IList<KeyValuePair<string, string>> values)
        {
            var lines = File.Exists(path) ? new List<string>(File.ReadAllLines(path)) : new List<string>();
            var done = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < lines.Count; i++)
            {
                var s = lines[i].Trim();
                if (s.Length == 0 || s[0] == ';' || s[0] == '#') continue;
                int eq = s.IndexOf('=');
                if (eq <= 0) continue;
                var key = s.Substring(0, eq).Trim();
                foreach (var v in values)
                {
                    if (string.Equals(v.Key, key, StringComparison.OrdinalIgnoreCase))
                    {
                        lines[i] = v.Key + "=" + v.Value;
                        done.Add(v.Key);
                    }
                }
            }
            foreach (var v in values)
                if (!done.Contains(v.Key)) lines.Add(v.Key + "=" + v.Value);
            File.WriteAllLines(path, lines.ToArray(), new UTF8Encoding(false));
        }
    }

    class LauncherForm : Form
    {
        static readonly Color Orange = Color.FromArgb(248, 136, 8);
        static readonly Color DimOrange = Color.FromArgb(120, 66, 4);
        static readonly Color Grey = Color.FromArgb(150, 150, 150);
        static readonly CultureInfo Inv = CultureInfo.InvariantCulture;

        string gameRoot;              // null on the setup screen
        bool updateOnPlay;            // this exe carries a different build than the game has
        DateTime uninstallArmed;      // the second press within 5 s removes the 3D add-on
        readonly List<string> iniPaths = new List<string>();
        readonly List<Item> items = new List<Item>();
        int selected;
        string status = "";
        readonly Font titleFont, itemFont, smallFont;
        readonly int rowH = 29, listTop = 150, valueX = 470;
        Point dragStart;

        // Gamepad: read through the game's own xinput1_3.dll proxy, so what the
        // launcher sees (and reacts to) is exactly what Rez will get.
        [StructLayout(LayoutKind.Sequential)]
        struct XState { public uint packet; public ushort buttons; public byte lt, rt; public short lx, ly, rx, ry; }
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        delegate uint XGetState(uint index, out XState state);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibrary(string path);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll")]
        static extern bool FreeLibrary(IntPtr module);
        XGetState padGet;
        IntPtr padLib;            // the game's REZ\xinput1_3.dll, loaded to read the pad
        bool padHooked;
        readonly Timer padTimer = new Timer { Interval = 30 };
        int padHeldDir;           // 1 up, 2 down, 3 left, 4 right
        DateTime padRepeatAt;
        ushort padLastButtons;
        string padText = "";

        public LauncherForm(string root, List<string> setupRoots)
        {
            string family = FontExists("Eurostile") ? "Eurostile" : (FontExists("Bahnschrift") ? "Bahnschrift" : "Segoe UI");
            titleFont = new Font(family, 40f, FontStyle.Bold, GraphicsUnit.Pixel);
            itemFont = new Font(family, 20f, FontStyle.Regular, GraphicsUnit.Pixel);
            smallFont = new Font(family, 14f, FontStyle.Regular, GraphicsUnit.Pixel);

            Text = "Rez Infinite 3D";
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.Black;
            DoubleBuffered = true;
            KeyPreview = true;

            if (root != null) EnterLauncher(root);
            else EnterSetup(setupRoots ?? new List<string>());
        }

        void EnterLauncher(string root)
        {
            gameRoot = root;
            Installer.RememberedRoot = Path.GetFullPath(root).TrimEnd('\\');
            // The game's own icon for the window and taskbar, read from the player's copy:
            // the release carries no game files, so Rez3D.exe has no icon of its own
            try
            {
                var game = Path.Combine(root, "Rez-infinite.exe");
                if (File.Exists(game)) { Icon = Icon.ExtractAssociatedIcon(game); ShowIcon = true; }
            }
            catch { }
            updateOnPlay = Installer.NeedsUpdate(root);
            iniPaths.Clear();
            iniPaths.Add(Path.Combine(root, @"REZ\rez3d.ini"));
            var ax = Path.Combine(root, @"AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64\rez3d.ini");
            if (File.Exists(ax)) iniPaths.Add(ax);
            ClientSize = new Size(860, 1030);
            items.Clear();
            selected = 0;
            BuildItems();
            Load_();
            if (updateOnPlay)
            {
                items[0].Help = "installs this build into the game first, then starts rez infinite in 3d";
                if (status.Length == 0) status = "this rez3d.exe is a different build: play installs it";
            }
            InitPad();
            Invalidate();
        }

        // Setup: shown when Rez3D.exe runs from the release zip. One entry per Rez Infinite
        // found (Steam libraries, GOG), plus browsing for another folder.
        void EnterSetup(List<string> roots)
        {
            ClientSize = new Size(860, 560);
            foreach (var r in roots)
            {
                var root = r;
                bool update = Installer.IsInstalled(root);
                items.Add(new Item { Label = update ? "update 3d" : "install 3d", Kind = Kind.Action, Value = root,
                                     Run = () => DoInstall(root),
                                     Help = update ? "update rez infinite 3d in this copy. your settings are kept"
                                                   : "add 3d to this copy. replaced game files are kept and restored by uninstall" });
            }
            items.Add(new Item { Label = "browse for the game", Kind = Kind.Action, Run = Browse,
                                 Help = "pick the folder that contains rez-infinite.exe" });
            items.Add(new Item { Label = "quit", Kind = Kind.Action, Run = Close, Help = "close without installing" });
            status = roots.Count > 0 ? "" : "rez infinite was not found: browse for its folder";
        }

        void DoInstall(string root)
        {
            try
            {
                Installer.Install(root);
                bool wasUpdate = items.Count > 0 && items[selected].Label == "update 3d";
                var location = Location;
                EnterLauncher(root);
                Location = location;
                status = wasUpdate ? "updated. ready to play" : "installed. pick your display format, then play";
            }
            catch (Exception e)
            {
                status = "install failed: " + e.Message.ToLowerInvariant();
            }
            Invalidate();
        }

        void Browse()
        {
            using (var dlg = new FolderBrowserDialog { Description = "Pick the Rez Infinite folder (it contains Rez-infinite.exe)" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                if (Installer.IsGameRoot(dlg.SelectedPath)) DoInstall(dlg.SelectedPath);
                else { status = "that folder has no rez-infinite.exe"; Invalidate(); }
            }
        }

        void Uninstall()
        {
            if (DateTime.Now > uninstallArmed)
            {
                uninstallArmed = DateTime.Now.AddSeconds(5);
                status = "press again to remove 3d and restore the original game files";
                Invalidate();
                return;
            }
            try
            {
                Installer.UninstallAfterExit(gameRoot);
                Close();
            }
            catch (Exception e)
            {
                status = "uninstall failed: " + e.Message.ToLowerInvariant();
                Invalidate();
            }
        }

        static bool FontExists(string name)
        {
            using (var f = new InstalledFontCollection())
                foreach (var fam in f.Families)
                    if (string.Equals(fam.Name, name, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }

        Item Num(string label, string key, double min, double max, double step, double def, int dec, string unit, string help)
        {
            var it = new Item { Label = label, Key = key, Kind = Kind.Number, Min = min, Max = max, Step = step,
                                Default = def, Decimals = dec, Unit = unit, Help = help };
            items.Add(it);
            return it;
        }

        Item Choice(string label, string key, string[] values, string[] labels, string def, string help)
        {
            var it = new Item { Label = label, Key = key, Kind = Kind.Choice, Choices = values, ChoiceLabels = labels,
                                DefaultChoice = def, Help = help };
            items.Add(it);
            return it;
        }

        void BuildItems()
        {
            items.Add(new Item { Label = "play", Kind = Kind.Action, Run = Play,
                                 Help = "start rez infinite in stereoscopic 3d" });
            Choice("format", "Format",
                   new[] { "Interlaced_Reversed", "Interlaced", "SBS", "SBS_Cross", "TAB", "TAB_Cross", "Checkerboard", "LeiaSR", "Mono_Left", "Mono_Right" },
                   new[] { "interlaced reversed", "interlaced", "side by side", "side by side cross", "top and bottom", "top and bottom cross", "checkerboard", "leia sr", "mono left", "mono right" },
                   "Interlaced_Reversed", "how both eyes are packed for your display. passive monitors: interlaced. leia sr: needs the sr platform");
            Num("separation", "Separation", 0, 200, 5, 100, 0, " %",
                "depth of distant objects. 100% = a real eye distance on your screen");
            Num("convergence", "Convergence", 1, 300, 1, 20, 1, " m",
                "rez: distance placed on the screen. ~20 keeps the avatar on the screen, higher pops out");
            Num("area x convergence", "AreaXConvergence", 0.5, 50, 0.1, 3, 1, " m",
                "area x is built closer: ~3 puts the crosshair on the screen, everything else behind it");
            Num("menu separation", "MenuSeparation", -5, 200, 5, -5, 0, " %",
                "experimental: lower depth in detected menus. can misfire in sparse gameplay");
            Num("field of view", "FovH", 60, 110, 5, 85, 0, "°",
                "horizontal view angle. 85 with lens shift 7 frames the avatar like the original game");
            Num("render scale", "RenderScale", 0.5, 2.0, 0.25, 1.5, 2, "x",
                "per eye resolution. above 1 = supersampling, less shimmer on thin lines");
            Num("interlace filter", "InterlaceFilter", 0, 1, 0.25, 0.5, 2, "",
                "blends each eye over the rows it owns. lower = sharper, more line flicker");
            Num("crosstalk", "Crosstalk", 0, 0.3, 0.005, 0, 3, "",
                "cancels ghosting from the glasses. raise until ghost copies of lines vanish");
            Num("crosstalk lift", "CrosstalkLift", 0, 1, 0.1, 0, 1, "",
                "lifts black so ghosts over black can be cancelled. greys the background");
            Num("brightness", "Brightness", 0.5, 2.0, 0.05, 1, 2, "x",
                "light gain to compensate for the glasses");
            Num("gamma", "Gamma", 0.5, 2.0, 0.05, 1, 2, "",
                "midtone brightness. above 1 = brighter midtones");
            int screens = Math.Max(1, Screen.AllScreens.Length);
            Num("monitor", "Monitor", 0, screens - 1, 1, 0, 0, "", "display to play on (0 = first)");
            Choice("vsync", "VSync", new[] { "1", "0" }, new[] { "on", "off" }, "1", "sync to the display refresh");
            Choice("frame pacing", "FramePacing", new[] { "1", "0" }, new[] { "on", "off" }, "1",
                   "hold the game to the display rate like a vr compositor");
            Num("lens shift", "LensShift", -20, 20, 1, 7, 0, " %",
                "moves the picture down. 7 frames the avatar like the original game, 0 is vr mode's view");
            Num("follow aim", "FollowAim", 0, 30, 5, 0, 0, "°",
                "the view turns toward your aim, like the original camera. 0 = off");
            Choice("head look", "Tracking", new[] { "off", "mouse" }, new[] { "off", "mouse" }, "off",
                   "experimental: move the camera with the mouse");
            Num("stick speed", "PadStickSpeed", 10, 100, 5, 60, 0, " %",
                "caps how far the game sees the stick tilt. lower = slower reticle than the game's own minimum");
            Num("stick curve", "PadStickCurve", 1, 3, 0.25, 1.5, 2, "",
                "above 1 = finer aim near the centre, full speed still at the edge");
            items.Add(new Item { Label = "defaults", Kind = Kind.Action, Run = Defaults,
                                 Help = "restore every value above to its default" });
            items.Add(new Item { Label = "uninstall 3d", Kind = Kind.Action, Run = Uninstall,
                                 Help = "remove rez infinite 3d from this copy and restore the original game files" });
            items.Add(new Item { Label = "quit", Kind = Kind.Action, Run = Close, Help = "close without playing" });
        }

        void Load_()
        {
            var kv = iniPaths.Count > 0 ? Ini.Read(iniPaths[0]) : new Dictionary<string, string>();
            foreach (var it in items)
            {
                string v;
                bool has = it.Key != null && kv.TryGetValue(it.Key, out v);
                v = has ? kv[it.Key] : null;
                if (it.Kind == Kind.Number)
                {
                    double d;
                    it.Num = has && double.TryParse(v, NumberStyles.Float, Inv, out d) ? d : it.Default;
                    if (it.Key == "MenuSeparation" && it.Num < 0) it.Num = it.Min;
                    it.Num = Math.Max(it.Min, Math.Min(it.Max, it.Num));
                }
                else if (it.Kind == Kind.Choice)
                {
                    it.ChoiceIndex = Math.Max(0, Array.FindIndex(it.Choices,
                        c => string.Equals(c, has ? v : it.DefaultChoice, StringComparison.OrdinalIgnoreCase)));
                }
            }
            if (!File.Exists(iniPaths[0])) status = "rez3d.ini not found: run rez3d.exe from the release zip to install";
        }

        string IniValue(Item it)
        {
            if (it.Kind == Kind.Choice) return it.Choices[it.ChoiceIndex];
            if (it.Key == "MenuSeparation" && it.Num < 0) return "-1";
            return it.Num.ToString("0.###", Inv);
        }

        void Save()
        {
            var values = new List<KeyValuePair<string, string>>();
            foreach (var it in items)
                if (it.Key != null) values.Add(new KeyValuePair<string, string>(it.Key, IniValue(it)));
            try
            {
                foreach (var p in iniPaths)
                    if (File.Exists(p)) Ini.Write(p, values);
                status = "saved";
            }
            catch (Exception e) { status = "save failed: " + e.Message.ToLowerInvariant(); }
        }

        string Display(Item it)
        {
            if (it.Kind == Kind.Choice) return it.ChoiceLabels[it.ChoiceIndex];
            if (it.Kind == Kind.Action)
            {
                // A long folder keeps its end: "...\steamapps\common\Rez Infinite"
                var v = (it.Value ?? "").ToLowerInvariant();
                return v.Length > 30 ? "..." + v.Substring(v.Length - 30) : v;
            }
            if (it.Key == "MenuSeparation" && it.Num < 0) return "off";
            if (it.Key == "Crosstalk" && it.Num == 0) return "off";
            return it.Num.ToString("F" + it.Decimals, Inv) + it.Unit;
        }

        void Change(Item it, int dir)
        {
            if (it.Kind == Kind.Number)
            {
                double step = it.Step;
                if (it.Key == "Convergence") step = it.Num < 10 ? 0.5 : (it.Num < 50 ? 1 : 5);
                if (it.Key == "AreaXConvergence") step = it.Num < 5 ? 0.1 : 0.5;
                double v = Math.Round((it.Num + dir * step) / step) * step;
                it.Num = Math.Max(it.Min, Math.Min(it.Max, Math.Round(v, 4)));
            }
            else if (it.Kind == Kind.Choice)
            {
                it.ChoiceIndex = (it.ChoiceIndex + dir + it.Choices.Length) % it.Choices.Length;
            }
            else return;
            Save();
            Invalidate();
        }

        void Play()
        {
            if (gameRoot == null) return;  // setup screen: nothing installed yet
            Save();
            if (updateOnPlay)
            {
                // Run from a build folder or a newer release: put this build in the game first
                ReleasePad();
                try { Installer.Install(gameRoot); updateOnPlay = false; }
                catch (Exception e)
                {
                    status = "could not update the game: " + e.Message.ToLowerInvariant();
                    InitPad();
                    Invalidate();
                    return;
                }
            }
            var exe = Path.Combine(gameRoot, "Rez-infinite.exe");
            if (!File.Exists(exe)) exe = Path.Combine(gameRoot, @"REZ\REZ.exe");
            try
            {
                var psi = new ProcessStartInfo(exe, "vr=1") { WorkingDirectory = Path.GetDirectoryName(exe), UseShellExecute = false };
                Process.Start(psi);
                Close();
            }
            catch (Exception e)
            {
                status = "could not start the game: " + e.Message.ToLowerInvariant();
                Invalidate();
            }
        }

        void Defaults()
        {
            foreach (var it in items)
            {
                if (it.Kind == Kind.Number) it.Num = it.Default;
                else if (it.Kind == Kind.Choice)
                    it.ChoiceIndex = Math.Max(0, Array.IndexOf(it.Choices, it.DefaultChoice));
            }
            Save();
            status = "defaults restored";
            Invalidate();
        }

        // Rez's menus use widely spaced lowercase type. Drawn a glyph at a time on
        // whole pixels with Windows' own text renderer: GDI+ grid-fitting each glyph
        // at a fractional position gave Eurostile's thin strokes uneven weights.
        static void DrawSpaced(Graphics g, string text, Font font, Color color, float x, float y, float spacing)
        {
            const TextFormatFlags flags = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix;
            var box = new Size(1000, 200);
            float space = TextRenderer.MeasureText(g, "x x", font, box, flags).Width -
                          TextRenderer.MeasureText(g, "xx", font, box, flags).Width;
            foreach (char c in text)
            {
                var s = c.ToString();
                if (c != ' ') TextRenderer.DrawText(g, s, font, new Point((int)Math.Round(x), (int)Math.Round(y)), color, flags);
                x += (c == ' ' ? space : TextRenderer.MeasureText(g, s, font, box, flags).Width) + spacing;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;

            // Title, with a thin wireframe-style rule like Rez's grid
            DrawSpaced(g, "rez", titleFont, Orange, 48, 34, 2);
            DrawSpaced(g, "infinite  3d", smallFont, Orange, 52, 86, 9);
            using (var pen = new Pen(DimOrange, 1))
            {
                g.DrawLine(pen, 48, 118, ClientSize.Width - 48, 118);
                for (int i = 0; i < 12; i++)
                    g.DrawLine(pen, ClientSize.Width - 48 - i * 22, 112, ClientSize.Width - 48 - i * 22, 124);
            }

            for (int i = 0; i < items.Count; i++)
            {
                var it = items[i];
                float y = listTop + i * rowH + (it.Kind == Kind.Action && i > 0 ? 14 : 0);
                bool sel = i == selected;
                var c = sel ? Color.White : Orange;
                DrawSpaced(g, it.Label, itemFont, c, 64, y, 3);
                if (it.Kind != Kind.Action)
                {
                    var val = Display(it);
                    DrawSpaced(g, sel ? "<  " + val + "  >" : "   " + val, itemFont, c, valueX, y, 2);
                }
                else if (it.Value != null)
                {
                    DrawSpaced(g, Display(it), smallFont, sel ? Color.White : Grey, 300, y + 4, 1);
                }
                if (sel)
                    using (var b = new SolidBrush(Orange)) g.FillRectangle(b, 46, y + 8, 6, 6);
            }

            float by = ClientSize.Height - (gameRoot == null ? 96 : 118);
            using (var pen = new Pen(DimOrange, 1)) g.DrawLine(pen, 48, by - 12, ClientSize.Width - 48, by - 12);
            DrawSpaced(g, items[selected].Help ?? "", smallFont, Grey, 48, by, 1);
            if (gameRoot == null)
            {
                DrawSpaced(g, "setup: needs your own steam or gog copy of rez infinite", smallFont, DimOrange, 48, by + 26, 1);
                DrawSpaced(g, status, smallFont, status.Length > 0 && status.Contains("fail") ? Color.White : DimOrange,
                           48, by + 48, 1);
                return;
            }
            DrawSpaced(g, "in game:  ctrl+f3/f4 separation   ctrl+f5/f6 convergence   ctrl+f9/f10 crosstalk",
                       smallFont, DimOrange, 48, by + 26, 1);
            DrawSpaced(g, "ctrl+f7 save   ctrl+t stereo on/off   ctrl+alt+f4 force quit", smallFont, DimOrange, 48, by + 48, 1);
            DrawSpaced(g, padText, smallFont, padText.StartsWith("pad: press") || padText == "pad: ok" ? Grey : Color.White,
                       48, by + 74, 1);
            // Status on its own line, white when something went wrong
            bool problem = status.Contains("could not") || status.Contains("fail") || status.Contains("not found");
            DrawSpaced(g, status, smallFont, problem ? Color.White : DimOrange, 48, by + 96, 1);
        }

        void InitPad()
        {
            try
            {
                Environment.SetEnvironmentVariable("REZPAD_NO_KEYBOARD", "1");  // arrows are handled as keys here
                padLib = LoadLibrary(Path.Combine(gameRoot, @"REZ\xinput1_3.dll"));
                var fn = padLib == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(padLib, "XInputGetState");
                if (fn == IntPtr.Zero) { padText = "pad: controller bridge not installed"; return; }
                padGet = (XGetState)Marshal.GetDelegateForFunctionPointer(fn, typeof(XGetState));
                padText = "pad: press any button on your controller to test it";
                if (!padHooked) { padTimer.Tick += (s, e) => PollPad(); padHooked = true; }
                padTimer.Start();
            }
            catch (Exception e) { padText = "pad: " + e.Message.ToLowerInvariant(); }
        }

        // The pad is read through the game's own xinput1_3.dll, and Windows won't replace
        // a DLL this process has loaded: let go of it while installing over the game
        void ReleasePad()
        {
            padTimer.Stop();
            padGet = null;
            if (padLib != IntPtr.Zero) { FreeLibrary(padLib); padLib = IntPtr.Zero; }
        }

        void PollPad()
        {
            if (!ContainsFocus) return;
            XState st;
            if (padGet(0, out st) != 0) return;
            const ushort Up = 0x1, Down = 0x2, Left = 0x4, Right = 0x8, Start = 0x10, A = 0x1000;
            ushort b = st.buttons;
            if (st.ly > 16000) b |= Up;
            if (st.ly < -16000) b |= Down;
            if (st.lx < -16000) b |= Left;
            if (st.lx > 16000) b |= Right;

            // Live readout of what the game would receive
            var names = new List<string>();
            string[] n = { "up", "down", "left", "right", "start", "back", "l3", "r3", "lb", "rb", "", "", "a", "b", "x", "y" };
            for (int i = 0; i < 16; i++)
                if ((st.buttons & (1 << i)) != 0 && n[i].Length > 0) names.Add(n[i]);
            if (st.lt > 0) names.Add("lt");
            if (st.rt > 0) names.Add("rt");
            if (st.lx != 0 || st.ly != 0) names.Add("left stick " + (st.lx * 100 / 32767) + "," + (st.ly * 100 / 32767));
            if (st.rx != 0 || st.ry != 0) names.Add("right stick " + (st.rx * 100 / 32767) + "," + (st.ry * 100 / 32767));
            string text = names.Count > 0 ? "pad: " + string.Join("  ", names.ToArray())
                                          : (padText.StartsWith("pad: press") ? padText : "pad: ok");
            if (text != padText) { padText = text; Invalidate(); }

            // Menu navigation: d-pad / left stick, a = select, start = play
            int dir = (b & Up) != 0 ? 1 : (b & Down) != 0 ? 2 : (b & Left) != 0 ? 3 : (b & Right) != 0 ? 4 : 0;
            if (dir != padHeldDir)
            {
                padHeldDir = dir;
                padRepeatAt = DateTime.Now.AddMilliseconds(400);
                if (dir != 0) PadMove(dir);
            }
            else if (dir != 0 && DateTime.Now >= padRepeatAt)
            {
                padRepeatAt = DateTime.Now.AddMilliseconds(110);
                PadMove(dir);
            }
            ushort pressed = (ushort)(st.buttons & ~padLastButtons);
            padLastButtons = st.buttons;
            if ((pressed & A) != 0)
            {
                var it = items[selected];
                if (it.Kind == Kind.Action) it.Run(); else Change(it, +1);
            }
            else if ((pressed & Start) != 0) Play();
        }

        void PadMove(int dir)
        {
            var it = items[selected];
            if (dir == 1) { selected = (selected + items.Count - 1) % items.Count; status = ""; Invalidate(); }
            else if (dir == 2) { selected = (selected + 1) % items.Count; status = ""; Invalidate(); }
            else Change(it, dir == 3 ? -1 : +1);
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            padTimer.Stop();
            base.OnFormClosed(e);
        }

        int RowAt(int y)
        {
            for (int i = 0; i < items.Count; i++)
            {
                float top = listTop + i * rowH + (items[i].Kind == Kind.Action && i > 0 ? 14 : 0);
                if (y >= top - 4 && y < top + rowH - 4) return i;
            }
            return -1;
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            var it = items[selected];
            switch (e.KeyCode)
            {
                case Keys.Up: selected = (selected + items.Count - 1) % items.Count; status = ""; Invalidate(); break;
                case Keys.Down: selected = (selected + 1) % items.Count; status = ""; Invalidate(); break;
                case Keys.Left: Change(it, -1); break;
                case Keys.Right: Change(it, +1); break;
                case Keys.Enter:
                case Keys.Space:
                    if (it.Kind == Kind.Action) it.Run(); else Change(it, +1);
                    break;
                case Keys.Escape: Close(); break;
            }
            e.Handled = true;
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            int row = RowAt(e.Y);
            if (row < 0)
            {
                dragStart = e.Location;  // drag the borderless window by its empty areas
                return;
            }
            selected = row;
            status = "";
            var it = items[row];
            if (it.Kind == Kind.Action) it.Run();
            else if (e.X >= valueX) Change(it, e.Button == MouseButtons.Right || e.X < valueX + 30 ? -1 : +1);
            Invalidate();
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Left && RowAt(dragStart.Y) < 0 && dragStart != Point.Empty)
                Location = new Point(Location.X + e.X - dragStart.X, Location.Y + e.Y - dragStart.Y);
        }

        protected override void OnMouseUp(MouseEventArgs e) { dragStart = Point.Empty; }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            int row = RowAt(e.Y);
            if (row >= 0) { selected = row; Change(items[row], e.Delta > 0 ? +1 : -1); }
        }
    }

    // Installs into / removes from a Rez Infinite folder, exactly like install.ps1 and
    // uninstall.ps1: replaced files are kept as *.orig, and rez3d-installed.txt records
    // "added|<path>" or "replaced|<path>" for every file touched. The files to install
    // sit in the bin\ folder next to this exe (build.ps1 puts them there and in the zip).
    static class Installer
    {
        public const string Manifest = "rez3d-installed.txt";
        const string AxVr = @"AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64";
        const string AxBin = @"AX\REZAX\Binaries\Win64";

        public static bool IsGameRoot(string dir)
        {
            return !string.IsNullOrEmpty(dir) && File.Exists(Path.Combine(dir, @"REZ\REZ.exe")) &&
                   File.Exists(Path.Combine(dir, "Rez-infinite.exe"));
        }

        public static bool IsInstalled(string root) { return File.Exists(Path.Combine(root, Manifest)); }

        // The game folder last installed into or launched, so a Rez3D.exe started from
        // anywhere (the release zip, a build folder, a shortcut) opens the launcher for it
        const string SettingsKey = @"HKEY_CURRENT_USER\Software\Rez Infinite 3D";
        public static string RememberedRoot
        {
            get
            {
                try { return Microsoft.Win32.Registry.GetValue(SettingsKey, "GameRoot", null) as string; }
                catch { return null; }
            }
            set
            {
                try { Microsoft.Win32.Registry.SetValue(SettingsKey, "GameRoot", value); } catch { }
            }
        }

        static string SelfPath { get { return System.Reflection.Assembly.GetExecutingAssembly().Location; } }

        static bool SameBytes(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;
            return true;
        }

        // True when this exe runs from somewhere else than root\Rez3D.exe
        public static bool RunningOutside(string root)
        {
            return !string.Equals(Path.GetFullPath(SelfPath), Path.Combine(Path.GetFullPath(root).TrimEnd('\\'), "Rez3D.exe"),
                                  StringComparison.OrdinalIgnoreCase);
        }

        // True when the game's installed files differ from the ones this exe carries:
        // play from a newer build (or the release zip) then installs them first
        public static bool NeedsUpdate(string root)
        {
            Func<string, byte[], bool> differs = (path, data) =>
            {
                if (data == null) return false;
                if (!File.Exists(path)) return true;
                var have = File.ReadAllBytes(path);
                if (have.Length != data.Length) return true;
                for (int i = 0; i < have.Length; i++) if (have[i] != data[i]) return true;
                return false;
            };
            try
            {
                var shim = Payload("openvr_api.dll", true);
                var pad = Payload("xinput1_3.dll", true);
                var leia = Payload("rez3d_leia_sr.dll", false);
                if (differs(Path.Combine(root, @"REZ\openvr_api.dll"), shim) ||
                    differs(Path.Combine(root, @"REZ\xinput1_3.dll"), pad) ||
                    differs(Path.Combine(root, @"REZ\rez3d_leia_sr.dll"), leia))
                    return true;
                var axvr = Path.Combine(root, AxVr);
                if (Directory.Exists(axvr) && (differs(Path.Combine(axvr, "openvr_api.dll"), shim) ||
                                               differs(Path.Combine(axvr, "rez3d_leia_sr.dll"), leia)))
                    return true;
                var axbin = Path.Combine(root, AxBin);
                if (Directory.Exists(axbin) && differs(Path.Combine(axbin, "xinput1_3.dll"), pad)) return true;
                return RunningOutside(root) && differs(Path.Combine(root, "Rez3D.exe"), File.ReadAllBytes(SelfPath));
            }
            catch { return false; }
        }

        // Steam libraries (libraryfolders.vdf) and GOG installs (registry), as game-path.ps1
        public static List<string> FindGameRoots()
        {
            var candidates = new List<string>();
            string steam = null;
            foreach (var key in new[] { @"HKEY_CURRENT_USER\Software\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam" })
            {
                steam = Microsoft.Win32.Registry.GetValue(key, "SteamPath", null) as string ??
                        Microsoft.Win32.Registry.GetValue(key, "InstallPath", null) as string;
                if (steam != null) break;
            }
            if (steam != null)
            {
                var libs = new List<string> { steam.Replace('/', '\\') };
                var vdf = Path.Combine(steam, @"steamapps\libraryfolders.vdf");
                try
                {
                    if (File.Exists(vdf))
                        foreach (System.Text.RegularExpressions.Match m in System.Text.RegularExpressions.Regex.Matches(
                                     File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                            libs.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
                }
                catch { }
                foreach (var lib in libs) candidates.Add(Path.Combine(lib, @"steamapps\common\Rez Infinite"));
            }
            foreach (var key in new[] { @"SOFTWARE\WOW6432Node\GOG.com\Games", @"SOFTWARE\GOG.com\Games" })
            {
                try
                {
                    using (var games = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(key))
                        if (games != null)
                            foreach (var id in games.GetSubKeyNames())
                                using (var g = games.OpenSubKey(id))
                                {
                                    var name = g.GetValue("gameName") as string;
                                    var path = g.GetValue("path") as string;
                                    if (name != null && path != null && name.StartsWith("Rez Infinite", StringComparison.OrdinalIgnoreCase))
                                        candidates.Add(path);
                                }
                }
                catch { }
            }
            var remembered = RememberedRoot;
            if (remembered != null) candidates.Insert(0, remembered);
            var found = new List<string>();
            foreach (var c in candidates)
            {
                if (!IsGameRoot(c)) continue;
                var full = Path.GetFullPath(c).TrimEnd('\\');
                if (!found.Exists(f => string.Equals(f, full, StringComparison.OrdinalIgnoreCase))) found.Add(full);
            }
            return found;
        }

        // A file to install, from the bin\ folder next to this exe (release zip, build folder)
        static byte[] Payload(string name, bool required)
        {
            var side = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"bin\" + name);
            if (File.Exists(side)) return File.ReadAllBytes(side);
            if (required)
                throw new FileNotFoundException("bin\\" + name + " not found: run rez3d.exe from the extracted release zip");
            return null;
        }

        public static string Install(string root)
        {
            root = Path.GetFullPath(root).TrimEnd('\\');
            if (!IsGameRoot(root)) throw new InvalidOperationException("not a rez infinite folder: " + root);
            var shim = Payload("openvr_api.dll", true);
            var pad = Payload("xinput1_3.dll", true);
            var leia = Payload("rez3d_leia_sr.dll", false);  // absent from builds without the SR SDK
            var ini = Payload("rez3d.ini", true);

            var manifest = Path.Combine(root, Manifest);
            bool firstInstall = !File.Exists(manifest);
            var entries = File.Exists(manifest) ? new List<string>(File.ReadAllLines(manifest)) : new List<string>();
            entries.RemoveAll(string.IsNullOrWhiteSpace);
            Func<string, string> rel = p => p.Substring(root.Length).TrimStart('\\');
            Func<string, bool> known = p => entries.Exists(e => e.EndsWith("|" + rel(p), StringComparison.OrdinalIgnoreCase));
            Action<string, byte[]> file = (to, data) =>
            {
                if (File.Exists(to) && !known(to))
                {
                    // An existing .orig is older than the file now in place (an earlier mod
                    // or manual install), so it is the one to restore: never overwrite it
                    if (!File.Exists(to + ".orig")) File.Copy(to, to + ".orig");
                    entries.Add("replaced|" + rel(to));
                }
                else if (!known(to)) entries.Add("added|" + rel(to));
                // Unchanged files stay untouched (one may be loaded by a running launcher or game)
                if (!File.Exists(to) || !SameBytes(File.ReadAllBytes(to), data)) File.WriteAllBytes(to, data);
            };
            // Settings: an update keeps the player's own ini
            Action<string> settings = dir =>
            {
                var to = Path.Combine(dir, "rez3d.ini");
                if (File.Exists(to)) return;
                var legacy = Path.Combine(dir, "rezstereo.ini");
                if (File.Exists(legacy)) File.Copy(legacy, to); else File.WriteAllBytes(to, ini);
                entries.Add("added|" + rel(to));
            };

            var rez = Path.Combine(root, "REZ");
            file(Path.Combine(rez, "openvr_api.dll"), shim);
            file(Path.Combine(rez, "xinput1_3.dll"), pad);
            if (leia != null) file(Path.Combine(rez, "rez3d_leia_sr.dll"), leia);
            settings(rez);
            var axvr = Path.Combine(root, AxVr);
            if (Directory.Exists(axvr))
            {
                file(Path.Combine(axvr, "openvr_api.dll"), shim);
                if (leia != null) file(Path.Combine(axvr, "rez3d_leia_sr.dll"), leia);
                settings(axvr);
            }
            var axbin = Path.Combine(root, AxBin);
            if (Directory.Exists(axbin)) file(Path.Combine(axbin, "xinput1_3.dll"), pad);

            // This exe becomes the game's launcher (and the runtime's watchdog)
            var self = System.Reflection.Assembly.GetExecutingAssembly().Location;
            var launcher = Path.Combine(root, "Rez3D.exe");
            if (!string.Equals(Path.GetFullPath(self), launcher, StringComparison.OrdinalIgnoreCase))
                file(launcher, File.ReadAllBytes(self));

            // Steam copies: started outside Steam, the game restarts itself through Steam and
            // loses the vr=1 argument. steam_appid.txt (Valve's documented way to run a Steam
            // game straight from its folder) keeps it in this process. First install only:
            // an update never brings back a file the player removed.
            var appid = Path.Combine(root, "steam_appid.txt");
            if (firstInstall && File.Exists(Path.Combine(root, "steam_api64.dll")) && !File.Exists(appid))
            {
                File.WriteAllText(appid, "636450", Encoding.ASCII);
                entries.Add("added|" + rel(appid));
            }
            File.WriteAllLines(manifest, entries.ToArray(), new UTF8Encoding(false));
            RememberedRoot = root;
            return "installed into " + root;
        }

        public static string Uninstall(string root, bool keepSettings)
        {
            root = Path.GetFullPath(root).TrimEnd('\\');
            var manifest = Path.Combine(root, Manifest);
            if (!File.Exists(manifest)) throw new InvalidOperationException("rez infinite 3d is not installed in " + root);
            int restored = 0, removed = 0;
            foreach (var line in File.ReadAllLines(manifest))
            {
                var parts = line.Split(new[] { '|' }, 2);
                if (parts.Length < 2 || parts[1].Length == 0) continue;
                var p = Path.Combine(root, parts[1]);
                if (parts[0] == "replaced" && File.Exists(p + ".orig"))
                {
                    if (File.Exists(p)) File.Delete(p);
                    File.Move(p + ".orig", p);
                    restored++;
                }
                else if (parts[0] == "added" && File.Exists(p))
                {
                    if (keepSettings && p.EndsWith("rez3d.ini", StringComparison.OrdinalIgnoreCase)) continue;
                    File.Delete(p);
                    removed++;
                }
            }
            File.Delete(manifest);
            // Logs and debug output written while playing
            foreach (var pattern in new[] { "rez3d.log", "rezstereo.log", "xinput_proxy.log", "rez3d_watchdog.log",
                                            "rez3d_frame_*.bmp", "rez3d_pair_*.bmp", "rezstereo_frame_*.bmp", "rezpad.txt" })
                foreach (var f in Directory.GetFiles(root, pattern, SearchOption.AllDirectories))
                    try { File.Delete(f); } catch { }
            return string.Format("restored {0} original file(s) and removed {1} added file(s)", restored, removed);
        }

        // The running Rez3D.exe can't restore or delete itself: a temporary copy waits for
        // this process to exit and does the work, then reports in a message box.
        public static void UninstallAfterExit(string root)
        {
            var tmp = Path.Combine(Path.GetTempPath(), "Rez3D-uninstall.exe");
            File.Copy(System.Reflection.Assembly.GetExecutingAssembly().Location, tmp, true);
            Process.Start(new ProcessStartInfo(tmp, string.Format("--uninstall \"{0}\" --wait-pid {1}", root,
                                                                  Process.GetCurrentProcess().Id))
                          { UseShellExecute = false });
        }
    }

    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            if (args.Length >= 1 && (args[0] == "--watchdog" || args[0] == "--watchdog-recover-now"))
            {
                Watchdog.Run(AppDomain.CurrentDomain.BaseDirectory, args[0] == "--watchdog-recover-now");
                return;
            }
            // Lives in the game root (next to Rez-infinite.exe); a path argument overrides
            string snapshot = null;
            if (args.Length >= 2 && args[0] == "--snapshot")
            {
                snapshot = args[1];
                var rest = new string[args.Length - 2];
                Array.Copy(args, 2, rest, 0, rest.Length);
                args = rest;
            }
            // Without a window: Rez3D.exe --install <game folder> / --uninstall <game folder>
            // [--keep-settings] [--quiet] [--wait-pid N] (the launcher's uninstall uses the last)
            if (args.Length >= 2 && (args[0] == "--install" || args[0] == "--uninstall"))
            {
                var flags = new List<string>(args);
                bool quiet = flags.Contains("--quiet");
                int wait = flags.IndexOf("--wait-pid");
                if (wait > 0 && wait + 1 < args.Length)
                    try { Process.GetProcessById(int.Parse(args[wait + 1])).WaitForExit(15000); } catch { }
                string msg;
                bool ok = true;
                try
                {
                    msg = args[0] == "--install" ? Installer.Install(args[1])
                                                 : Installer.Uninstall(args[1], flags.Contains("--keep-settings"));
                }
                catch (Exception e) { msg = "failed: " + e.Message; ok = false; }
                Console.WriteLine(msg);
                if (!quiet && args[0] == "--uninstall")
                    MessageBox.Show(ok ? "Rez Infinite 3D was removed: " + msg + "." : "Uninstall " + msg, "Rez Infinite 3D",
                                    MessageBoxButtons.OK, ok ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
                Environment.Exit(ok ? 0 : 1);
            }
            string root = args.Length > 0 ? args[0] : AppDomain.CurrentDomain.BaseDirectory;
            if (!File.Exists(Path.Combine(root, @"REZ\REZ.exe")))
            {
                var sibling = Path.GetFullPath(Path.Combine(root, @"..\Rez Infinite"));
                if (File.Exists(Path.Combine(sibling, @"REZ\REZ.exe"))) root = sibling;
            }
            Application.EnableVisualStyles();
            // Started outside a game folder (a build folder, the release zip, a shortcut):
            // the game this exe last installed into or launched
            if (!Installer.IsGameRoot(root))
            {
                var remembered = Installer.RememberedRoot;
                if (Installer.IsGameRoot(remembered) && Installer.IsInstalled(remembered)) root = remembered;
            }
            // A game without 3D yet, or none known: the setup screen
            List<string> setupRoots = null;
            if (!Installer.IsGameRoot(root) || !Installer.IsInstalled(root))
            {
                setupRoots = Installer.IsGameRoot(root) ? new List<string> { Path.GetFullPath(root).TrimEnd('\\') }
                                                        : Installer.FindGameRoots();
                root = null;
            }
            var form = new LauncherForm(root, setupRoots);
            if (snapshot != null)
            {
                // Testing: render the launcher to an image without ever showing it
                using (var bmp = new Bitmap(form.ClientSize.Width, form.ClientSize.Height))
                {
                    form.DrawToBitmap(bmp, new Rectangle(Point.Empty, form.ClientSize));
                    bmp.Save(snapshot, System.Drawing.Imaging.ImageFormat.Png);
                }
                return;
            }
            Application.Run(form);
        }
    }

    // A way out that never depends on the game. Rez-infinite.exe keeps a topmost
    // fullscreen window and hides the taskbar, so when a game process hangs, Alt+Tab
    // and even Task Manager stay behind black. The runtime starts this next to the
    // game (one per session); it exits when the game does.
    //   Ctrl+Alt+F4: end every Rez process, show the taskbar, restore the desktop mode
    //   automatically when the launcher's window is left alone for 20 s (nothing to play)
    static class Watchdog
    {
        [DllImport("user32.dll")] static extern bool RegisterHotKey(IntPtr hWnd, int id, uint mods, uint vk);
        [DllImport("user32.dll")] static extern IntPtr FindWindow(string cls, string title);
        [DllImport("user32.dll")] static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
        [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hWnd, int cmd);
        [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr hWnd, out int pid);
        [DllImport("user32.dll")] static extern int ChangeDisplaySettings(IntPtr mode, int flags);
        [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder name, ref int size);

        const uint MOD_ALT = 1, MOD_CONTROL = 2, MOD_NOREPEAT = 0x4000, VK_F4 = 0x73;
        const int WM_HOTKEY = 0x0312;
        static readonly string[] Names = { "REZAX-Win64-Shipping", "REZAX", "REZ", "Rez-infinite" };
        static string root;
        static StreamWriter log;

        static void Log(string text)
        {
            try { log.WriteLine(DateTime.Now.ToString("HH:mm:ss.fff") + " " + text); log.Flush(); } catch { }
        }

        static string ImagePath(Process p)
        {
            IntPtr h = OpenProcess(0x1000, false, p.Id);  // PROCESS_QUERY_LIMITED_INFORMATION
            if (h == IntPtr.Zero) return null;
            try
            {
                var sb = new StringBuilder(1024);
                int size = sb.Capacity;
                return QueryFullProcessImageName(h, 0, sb, ref size) ? sb.ToString() : null;
            }
            finally { CloseHandle(h); }
        }

        // This game's processes only (another copy of Rez elsewhere is left alone)
        static List<Process> Game(string name)
        {
            var list = new List<Process>();
            foreach (var p in Process.GetProcessesByName(name))
            {
                var path = ImagePath(p);
                if (path != null && path.StartsWith(root, StringComparison.OrdinalIgnoreCase)) list.Add(p);
            }
            return list;
        }

        static void Recover(string why)
        {
            Log("recovering: " + why);
            foreach (var name in Names)
                foreach (var p in Game(name))
                {
                    try { p.Kill(); p.WaitForExit(3000); Log("ended " + name + " (" + p.Id + ")"); }
                    catch (Exception e) { Log("could not end " + name + ": " + e.Message); }
                }
            // Rez-infinite.exe hid the taskbar (and the classic Start button)
            foreach (var cls in new[] { "Shell_TrayWnd", "Shell_SecondaryTrayWnd" })
                for (IntPtr w = FindWindowEx(IntPtr.Zero, IntPtr.Zero, cls, null); w != IntPtr.Zero;
                     w = FindWindowEx(IntPtr.Zero, w, cls, null))
                    ShowWindow(w, 5);  // SW_SHOW
            IntPtr start = FindWindow("Button", null);
            if (start != IntPtr.Zero)
            {
                int pid;
                GetWindowThreadProcessId(start, out pid);
                try { if (Process.GetProcessById(pid).ProcessName.Equals("explorer", StringComparison.OrdinalIgnoreCase)) ShowWindow(start, 5); }
                catch { }
            }
            ChangeDisplaySettings(IntPtr.Zero, 0);  // back to the desktop's own mode
            Log("taskbar shown, display mode restored");
        }

        class HotkeyFilter : IMessageFilter
        {
            public bool PreFilterMessage(ref Message m)
            {
                if (m.Msg != WM_HOTKEY) return false;
                Recover("Ctrl+Alt+F4");
                Application.Exit();
                return true;
            }
        }

        public static void Run(string gameRoot, bool recoverNow)
        {
            root = Path.GetFullPath(gameRoot).TrimEnd('\\') + "\\";
            bool first;
            using (var single = new System.Threading.Mutex(true, @"Local\Rez3DWatchdog", out first))
            {
                if (!first && !recoverNow) return;
                // Shared, so a --watchdog-recover-now run can log next to the running one
                try
                {
                    var file = Path.Combine(root, @"REZ\rez3d_watchdog.log");
                    log = new StreamWriter(new FileStream(file, first && !recoverNow ? FileMode.Create : FileMode.Append,
                                                          FileAccess.Write, FileShare.ReadWrite));
                }
                catch { log = StreamWriter.Null; }
                if (recoverNow) { Recover("requested (--watchdog-recover-now)"); return; }
                bool hotkey = RegisterHotKey(IntPtr.Zero, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F4);
                Log("watching " + root + (hotkey ? ", Ctrl+Alt+F4 ready" : ", Ctrl+Alt+F4 unavailable (taken by another program)"));
                Application.AddMessageFilter(new HotkeyFilter());
                int idle = 0, alone = 0;
                var timer = new Timer { Interval = 1000 };
                timer.Tick += (s, e) =>
                {
                    bool launcher = Game("Rez-infinite").Count > 0;
                    bool playing = Game("REZ").Count + Game("REZAX").Count + Game("REZAX-Win64-Shipping").Count > 0;
                    // The game has closed: nothing left to watch
                    idle = launcher || playing ? 0 : idle + 1;
                    if (idle >= 3) { Log("game closed"); Application.Exit(); return; }
                    // Only the launcher's black fullscreen window is left
                    alone = launcher && !playing ? alone + 1 : 0;
                    if (alone >= 20) { Recover("the launcher was left alone for 20 s"); Application.Exit(); }
                };
                timer.Start();
                Application.Run();
            }
        }
    }
}
