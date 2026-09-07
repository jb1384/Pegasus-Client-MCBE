using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

internal static class Program {
    internal const string DllName = "BedrockUtilityFramework.Xray.dll";
    internal const string ExpectedHash = "3EB3AC8077F692FF755FE99C5CF8A05D326F7ABCCABCB19400932D63EBEDF0F6";
    internal static string DllPath { get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, DllName); } }
    [STAThread] static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--test-host") { Thread.Sleep(30000); return 0; }
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        if (args.Length == 1 && args[0] == "--self-test") {
            try {
                VerifyDll();
                using (Process host = Process.Start(new ProcessStartInfo(Application.ExecutablePath, "--test-host") { UseShellExecute = false, CreateNoWindow = true })) {
                    try { Thread.Sleep(1000); Loader.Inject(host, DllPath); }
                    finally { if (!host.HasExited) host.Kill(); host.WaitForExit(); }
                }
                File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "self-test-result.txt"), "PASS: verified bundled DLL hash and loaded it into a disposable x64 test process. No game process was touched.");
                return 0;
            } catch (Exception e) {
                File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "self-test-result.txt"), "FAIL: " + e.Message); return 1;
            }
        }
        if (args.Length == 2 && args[0] == "--preview") {
            using (var form = new PegasusForm()) {
                form.Show(); form.Refresh();
                using (var bmp = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(bmp, new Rectangle(Point.Empty, bmp.Size)); bmp.Save(args[1]); }
            }
            return 0;
        }
        Application.Run(new PegasusForm()); return 0;
    }
    internal static void VerifyDll() {
        if (!File.Exists(DllPath)) throw new IOException("Keep the included DLL beside Pegasus.exe.");
        using (var stream = File.OpenRead(DllPath)) using (var hash = SHA256.Create()) {
            if (BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "") != ExpectedHash)
                throw new IOException("The DLL does not match this release. Restore the original bundled DLL.");
        }
    }
}

internal sealed class PegasusButton : Button {
    protected override void OnPaint(PaintEventArgs e) {
        e.Graphics.Clear(Enabled ? BackColor : Color.FromArgb(50, 42, 76));
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle,
            Enabled ? Color.White : Color.FromArgb(182, 166, 219), TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        if (Focused) ControlPaint.DrawFocusRectangle(e.Graphics, Rectangle.Inflate(ClientRectangle, -4, -4));
    }
}

internal sealed class PegasusForm : Form {
    readonly Color muted = Color.FromArgb(161, 164, 190);
    readonly ComboBox targets = new ComboBox();
    readonly Label status = new Label();
    readonly Button load = new PegasusButton();
    readonly Button refresh = new PegasusButton();
    sealed class Target {
        public int Id; public string Name;
        public override string ToString() { return Name + "   /   PID " + Id; }
    }
    internal PegasusForm() {
        Text = "pegasus | utility mod 1.0";
        ClientSize = new Size(580, 490); FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false; StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(15, 17, 27); ForeColor = Color.White;
        Font = new Font("Segoe UI", 10); AutoScaleMode = AutoScaleMode.Dpi;
        DoubleBuffered = true;
        AddLabel("pegasus", 32, 23, 430, 63, 36, Color.White, FontStyle.Bold);
        AddLabel("UTILITY MOD   /   1.0", 36, 93, 440, 25, 10, Color.FromArgb(188, 169, 252), FontStyle.Bold);
        AddLabel("BETA VERSION", 36, 137, 500, 26, 13, Color.White, FontStyle.Bold);
        AddLabel("MINECRAFT BEDROCK  ·  X64", 36, 174, 510, 23, 9, muted, FontStyle.Bold);
        AddLabel("GAME SESSION", 36, 224, 430, 20, 9, muted, FontStyle.Bold);
        targets.SetBounds(36, 253, 390, 32); targets.DropDownStyle = ComboBoxStyle.DropDownList;
        targets.BackColor = Color.FromArgb(32, 35, 51); targets.ForeColor = Color.White; targets.FlatStyle = FlatStyle.Flat;
        targets.DrawMode = DrawMode.OwnerDrawFixed; targets.ItemHeight = 25;
        targets.DrawItem += delegate(object sender, DrawItemEventArgs e) {
            using (var brush = new SolidBrush(Color.FromArgb(32,35,51))) e.Graphics.FillRectangle(brush, e.Bounds);
            string value = e.Index >= 0 ? targets.Items[e.Index].ToString() : "No game session found";
            TextRenderer.DrawText(e.Graphics, value, targets.Font, e.Bounds, Color.FromArgb(220,220,235), TextFormatFlags.Left | TextFormatFlags.VerticalCenter);
        };
        Controls.Add(targets);
        SetupButton(refresh, "Refresh", 438, 251, 106, 34, Color.FromArgb(41, 43, 61));
        refresh.Click += delegate { RefreshTargets(); };
        SetupButton(load, "Load utility mod   →", 36, 309, 508, 49, Color.FromArgb(117, 78, 219));
        load.Click += async delegate { await LoadSelected(); };
        status.SetBounds(36, 376, 508, 56); status.ForeColor = muted; status.Font = new Font("Segoe UI", 10);
        Controls.Add(status);
        AddLabel("SMOOTH JETPACK BUILD", 36, 451, 300, 19, 8, muted, FontStyle.Bold);
        AddLabel("PEGASUS  /  01", 423, 451, 140, 19, 8, muted, FontStyle.Bold);
        RefreshTargets();
    }
    void AddLabel(string text, int x, int y, int w, int h, float size, Color color, FontStyle style) {
        Controls.Add(new Label { Text = text, Bounds = new Rectangle(x,y,w,h), Font = new Font("Segoe UI",size,style), ForeColor = color, BackColor = Color.Transparent });
    }
    void SetupButton(Button button, string text, int x, int y, int w, int h, Color color) {
        button.Text = text; button.SetBounds(x,y,w,h); button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0; button.BackColor = color; button.ForeColor = Color.White;
        button.Font = new Font("Segoe UI", 11, FontStyle.Bold); button.Cursor = Cursors.Hand; Controls.Add(button);
    }
    protected override void OnPaint(PaintEventArgs e) {
        base.OnPaint(e);
        using (var brush = new LinearGradientBrush(new Rectangle(0,0,Width,5), Color.FromArgb(105,75,240), Color.FromArgb(85,211,232), 0f)) e.Graphics.FillRectangle(brush,0,0,Width,5);
        using (var pen = new Pen(Color.FromArgb(44,46,66))) { e.Graphics.DrawLine(pen,36,207,544,207); e.Graphics.DrawLine(pen,36,440,544,440); }
    }
    void RefreshTargets() {
        targets.Items.Clear();
        foreach (var p in Process.GetProcessesByName("Minecraft.Windows")) {
            using (p) { targets.Items.Add(new Target { Id = p.Id, Name = "Minecraft Bedrock" }); }
        }
        if (targets.Items.Count == 0) targets.Items.Add("No game session found");
        targets.SelectedIndex = 0;
        load.Enabled = targets.SelectedItem is Target;
        status.Text = load.Enabled ? "Ready. Use a fresh game session before loading." : "Open Minecraft Bedrock, then select Refresh.";
    }
    async Task LoadSelected() {
        var selected = targets.SelectedItem as Target; if (selected == null) return;
        load.Enabled = refresh.Enabled = targets.Enabled = false;
        status.Text = "Checking the game and loading the utility mod…";
        try {
            await Task.Run(delegate {
                Program.VerifyDll();
                using (var process = Process.GetProcessById(selected.Id)) {
                    if (!String.Equals(process.ProcessName, "Minecraft.Windows", StringComparison.OrdinalIgnoreCase)) throw new IOException("The selected session has ended. Refresh the list.");
                    // Packaged Minecraft needs read access to this one DLL.
                    var acl = File.GetAccessControl(Program.DllPath);
                    acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier("S-1-15-2-1"), FileSystemRights.ReadAndExecute, AccessControlType.Allow));
                    File.SetAccessControl(Program.DllPath, acl);
                    Loader.Inject(process, Program.DllPath);
                }
            });
            status.Text = "Loaded successfully. In Minecraft, press Tab to open the menu.";
        } catch (Exception e) { status.Text = e.Message; }
        finally { refresh.Enabled = targets.Enabled = true; load.Enabled = targets.SelectedItem is Target; }
    }
}

internal static class Loader {
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, ExactSpelling=true)] static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protect);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stack, IntPtr start, IntPtr parameter, uint flags, IntPtr id);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool IsWow64Process(IntPtr process, out bool wow64);
    internal static void Inject(Process process, string dll) {
        IntPtr handle = OpenProcess(0x043A, false, process.Id);
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot open the game. Try running Pegasus with the same permissions as Minecraft.");
        IntPtr memory = IntPtr.Zero, thread = IntPtr.Zero; bool mayFree = true;
        try {
            bool wow64;
            if (!IsWow64Process(handle, out wow64)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (wow64) throw new IOException("The selected process must be 64-bit.");
            IntPtr local = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryW");
            if (local == IntPtr.Zero) throw new IOException("Windows loader is unavailable.");
            string owner = null; long offset = 0;
            using (var current = Process.GetCurrentProcess()) {
                foreach (ProcessModule m in current.Modules) {
                    long delta = local.ToInt64() - m.BaseAddress.ToInt64();
                    if (delta >= 0 && delta < m.ModuleMemorySize) { owner = m.ModuleName; offset = delta; break; }
                }
            }
            IntPtr remote = IntPtr.Zero;
            process.Refresh();
            foreach (ProcessModule m in process.Modules) {
                if (m.ModuleName.StartsWith("BedrockUtilityFramework", StringComparison.OrdinalIgnoreCase) || String.Equals(m.FileName, dll, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("A utility DLL is already loaded. Restart Minecraft first.");
                if (String.Equals(m.ModuleName, owner, StringComparison.OrdinalIgnoreCase)) remote = new IntPtr(m.BaseAddress.ToInt64() + offset);
            }
            if (remote == IntPtr.Zero) throw new IOException("Could not locate the Windows loader in the selected process.");
            byte[] path = Encoding.Unicode.GetBytes(Path.GetFullPath(dll) + "\0");
            memory = VirtualAllocEx(handle, IntPtr.Zero, (UIntPtr)path.Length, 0x3000, 4);
            if (memory == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            UIntPtr written;
            if (!WriteProcessMemory(handle, memory, path, (UIntPtr)path.Length, out written) || written.ToUInt64() != (ulong)path.Length) throw new Win32Exception(Marshal.GetLastWin32Error());
            thread = CreateRemoteThread(handle, IntPtr.Zero, UIntPtr.Zero, remote, memory, 0, IntPtr.Zero);
            if (thread == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            mayFree = false;
            uint wait = WaitForSingleObject(thread, 15000);
            if (wait != 0) throw new IOException("The load has not completed. Restart the game before retrying.");
            mayFree = true;
            process.Refresh(); bool loaded = false;
            foreach (ProcessModule m in process.Modules) if (String.Equals(m.FileName, dll, StringComparison.OrdinalIgnoreCase)) loaded = true;
            if (!loaded) throw new IOException("Windows could not load the DLL. Check C++ debug runtimes and folder access; see README.");
        } finally {
            if (thread != IntPtr.Zero) CloseHandle(thread);
            if (memory != IntPtr.Zero && mayFree) VirtualFreeEx(handle, memory, UIntPtr.Zero, 0x8000);
            CloseHandle(handle);
        }
    }
}
