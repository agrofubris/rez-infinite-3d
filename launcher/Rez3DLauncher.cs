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

        readonly string gameRoot;
        readonly List<string> iniPaths = new List<string>();
        readonly List<Item> items = new List<Item>();
        int selected;
        string status = "";
        readonly Font titleFont, itemFont, smallFont;
        readonly int rowH = 30, listTop = 150, valueX = 470;
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
        XGetState padGet;
        readonly Timer padTimer = new Timer { Interval = 30 };
        int padHeldDir;           // 1 up, 2 down, 3 left, 4 right
        DateTime padRepeatAt;
        ushort padLastButtons;
        string padText = "";

        public LauncherForm(string root)
        {
            gameRoot = root;
            iniPaths.Add(Path.Combine(root, @"REZ\rez3d.ini"));
            var ax = Path.Combine(root, @"AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64\rez3d.ini");
            if (File.Exists(ax)) iniPaths.Add(ax);

            string family = FontExists("Eurostile") ? "Eurostile" : (FontExists("Bahnschrift") ? "Bahnschrift" : "Segoe UI");
            titleFont = new Font(family, 40f, FontStyle.Bold, GraphicsUnit.Pixel);
            itemFont = new Font(family, 20f, FontStyle.Regular, GraphicsUnit.Pixel);
            smallFont = new Font(family, 14f, FontStyle.Regular, GraphicsUnit.Pixel);

            Text = "Rez Infinite 3D";
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(860, 1000);
            BackColor = Color.Black;
            DoubleBuffered = true;
            KeyPreview = true;

            BuildItems();
            Load_();
            InitPad();
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
                "distance placed on the screen. ~20 keeps the avatar on the screen, higher pops out");
            Num("menu separation", "MenuSeparation", -5, 200, 5, -5, 0, " %",
                "experimental: lower depth in detected menus. can misfire in sparse gameplay");
            Num("field of view", "FovH", 60, 110, 5, 90, 0, "°",
                "horizontal view angle. below 90 the avatar starts to leave the bottom of the screen");
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
            if (!File.Exists(iniPaths[0])) status = "rez3d.ini not found: run install.ps1 first";
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
            if (it.Kind == Kind.Action) return "";
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
            Save();
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

        // Rez's menus use widely spaced lowercase type
        static void DrawSpaced(Graphics g, string text, Font font, Color color, float x, float y, float spacing)
        {
            using (var b = new SolidBrush(color))
            {
                var fmt = StringFormat.GenericTypographic;
                fmt.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces;
                foreach (char c in text)
                {
                    var s = c.ToString();
                    g.DrawString(s, font, b, x, y, fmt);
                    x += g.MeasureString(s, font, PointF.Empty, fmt).Width + spacing;
                }
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
                if (sel)
                    using (var b = new SolidBrush(Orange)) g.FillRectangle(b, 46, y + 8, 6, 6);
            }

            float by = ClientSize.Height - 96;
            using (var pen = new Pen(DimOrange, 1)) g.DrawLine(pen, 48, by - 12, ClientSize.Width - 48, by - 12);
            DrawSpaced(g, items[selected].Help ?? "", smallFont, Grey, 48, by, 1);
            DrawSpaced(g, "in game:  ctrl+f3/f4 separation   ctrl+f5/f6 convergence   ctrl+f9/f10 crosstalk",
                       smallFont, DimOrange, 48, by + 26, 1);
            DrawSpaced(g, "ctrl+f7 save   ctrl+t stereo on/off        " + status, smallFont, DimOrange, 48, by + 48, 1);
            DrawSpaced(g, padText, smallFont, padText.StartsWith("pad: press") || padText == "pad: ok" ? Grey : Color.White,
                       48, by + 74, 1);
        }

        void InitPad()
        {
            try
            {
                Environment.SetEnvironmentVariable("REZPAD_NO_KEYBOARD", "1");  // arrows are handled as keys here
                var lib = LoadLibrary(Path.Combine(gameRoot, @"REZ\xinput1_3.dll"));
                var fn = lib == IntPtr.Zero ? IntPtr.Zero : GetProcAddress(lib, "XInputGetState");
                if (fn == IntPtr.Zero) { padText = "pad: controller bridge not installed"; return; }
                padGet = (XGetState)Marshal.GetDelegateForFunctionPointer(fn, typeof(XGetState));
                padText = "pad: press any button on your controller to test it";
                padTimer.Tick += (s, e) => PollPad();
                padTimer.Start();
            }
            catch (Exception e) { padText = "pad: " + e.Message.ToLowerInvariant(); }
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

    static class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            // Lives in the game root (next to Rez-infinite.exe); a path argument overrides
            string snapshot = null;
            if (args.Length >= 2 && args[0] == "--snapshot")
            {
                snapshot = args[1];
                var rest = new string[args.Length - 2];
                Array.Copy(args, 2, rest, 0, rest.Length);
                args = rest;
            }
            string root = args.Length > 0 ? args[0] : AppDomain.CurrentDomain.BaseDirectory;
            if (!File.Exists(Path.Combine(root, @"REZ\REZ.exe")))
            {
                var sibling = Path.GetFullPath(Path.Combine(root, @"..\Rez Infinite"));
                if (File.Exists(Path.Combine(sibling, @"REZ\REZ.exe"))) root = sibling;
            }
            Application.EnableVisualStyles();
            var form = new LauncherForm(root);
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
}
