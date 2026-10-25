using Ass.Graphics;
using Ass.Graphics.WinForms;
using Color = Ass.Graphics.Color;
internal static class Program
{
    [STAThread]
    static unsafe void Main(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
        using var context = new GraphicsContext();
        using var theme = new ApplicationTheme(context);
        using var form =
            new Form { Text = "ASS Graphics — C# WinForms", ClientSize = new System.Drawing.Size(850, 550) };
        using var canvas = new CanvasControl(context) { Dock = DockStyle.Fill };
        using var status = new StatusTextBox(context) { Dock = DockStyle.Bottom, Height = 100 };
        form.Controls.Add(canvas);
        form.Controls.Add(status);
        theme.Apply(form);
        Draw[] draws = [
            Draw.Label("B612 • C# calls the native Direct2D renderer", new Rect(24, 24, 780, 70),
                       new Color(.1f, .12f, .15f), 22),
            Draw.Label("Shared fonts, controls and drawing behavior", new Rect(24, 80, 780, 115),
                       new Color(.16f, .4f, .78f), 16)
        ];
        Draw segments =
            new() { Kind = DrawKind.Segments, Bounds = new(24, 150, 560, 250), Color = new(.16f, .55f, .4f) };
        segments.SetText("1234.56");
        canvas.Submit([..draws, segments]);
        form.Shown += (_, _) =>
            status.Submit("Ready\r\nNative copyable live status; no simulator dependency.");
        using var timer = new System.Windows.Forms.Timer { Interval = 350 };
        timer.Tick += (_, _) =>
            status.Submit($"Live status\r\n{DateTime.Now:HH:mm:ss} — selection and scrolling are preserved.");
        timer.Start();
        if (args.Length > 0 && args[0] == "--smoke")
        {
            using var stop = new System.Windows.Forms.Timer { Interval = 800 };
            stop.Tick += (_, _) =>
            {
                stop.Stop();
                canvas.ExportPng(Path.Combine(AppContext.BaseDirectory, "managed-smoke.png"), 850, 450);
                form.Close();
            };
            stop.Start();
            Application.Run(form);
        }
        else
            Application.Run(form);
    }
}
