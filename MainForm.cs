using System.Drawing;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Windows.Forms;

namespace WlanIp;

internal sealed class MainForm : Form
{
    private readonly Label display;
    private readonly System.Windows.Forms.Timer timer;
    private string? currentIp;
    private Point dragStart;

    public MainForm()
    {
        FormBorderStyle = FormBorderStyle.None;
        ShowInTaskbar = false;
        TopMost = true;
        StartPosition = FormStartPosition.Manual;
        Location = new Point(30, 30);
        BackColor = Color.FromArgb(24, 24, 27);
        Padding = new Padding(14, 8, 14, 8);
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;

        display = new Label
        {
            AutoSize = true,
            BackColor = Color.Transparent,
            ForeColor = Color.White,
            Font = new Font("Microsoft YaHei UI", 11),
            Text = "● 获取中..."
        };
        Controls.Add(display);

        var menu = new ContextMenuStrip();
        menu.Items.Add("退出", null, (_, _) => Close());
        ContextMenuStrip = menu;
        display.ContextMenuStrip = menu;

        display.MouseDown += (_, e) =>
        {
            if (e.Button == MouseButtons.Left)
                dragStart = e.Location;
        };
        display.MouseMove += (_, e) =>
        {
            if (e.Button == MouseButtons.Left)
                Location = new Point(
                    Cursor.Position.X - dragStart.X - Padding.Left,
                    Cursor.Position.Y - dragStart.Y - Padding.Top);
        };
        display.DoubleClick += (_, _) =>
        {
            if (currentIp is not null)
                Clipboard.SetText(currentIp);
        };

        timer = new System.Windows.Forms.Timer { Interval = 3000 };
        timer.Tick += (_, _) => RefreshIp();
        timer.Start();
        RefreshIp();
    }

    private void RefreshIp()
    {
        currentIp = FindWirelessIpv4();
        display.Text = currentIp is null ? "● 未连接" : "● " + currentIp;
        display.ForeColor = currentIp is null ? Color.Gray : Color.FromArgb(134, 239, 172);
    }

    private static string? FindWirelessIpv4()
    {
        try
        {
            foreach (var nic in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (nic.NetworkInterfaceType != NetworkInterfaceType.Wireless80211 ||
                    nic.OperationalStatus != OperationalStatus.Up)
                    continue;

                foreach (var address in nic.GetIPProperties().UnicastAddresses)
                {
                    IPAddress ip = address.Address;
                    if (ip.AddressFamily == AddressFamily.InterNetwork &&
                        !IPAddress.IsLoopback(ip) &&
                        !ip.GetAddressBytes().AsSpan(0, 2).SequenceEqual(new byte[] { 169, 254 }))
                        return ip.ToString();
                }
            }
        }
        catch (NetworkInformationException)
        {
            // An adapter can disappear while Windows enumerates interfaces.
        }
        return null;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            timer.Dispose();
            display.Dispose();
        }
        base.Dispose(disposing);
    }
}
