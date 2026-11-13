using GameReaderCommon;
using SimHub.Plugins;
using System;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media;

namespace SimHub.CYDDashboard
{
    [PluginName("CYD Dashboard")]
    [PluginDescription("Sends SimHub telemetry to the CYD display and receives its touch-menu commands.")]
    [PluginAuthor("SimHub Yellow Dashboard")]
    public class CYDDashboardPlugin : IPlugin, IDataPlugin
    {
        private const string CommandType = "simhub_command";
        private static readonly Regex CommandRegex = new Regex(
            "\\\"command\\\"\\s*:\\s*\\\"([a-z_]+)\\\"",
            RegexOptions.Compiled | RegexOptions.CultureInvariant);

        private readonly object telemetryLock = new object();
        private readonly TelemetrySnapshot latestTelemetry = new TelemetrySnapshot();
        private PluginSettings settings;
        private UdpClient telemetrySocket;
        private UdpClient commandSocket;
        private IPEndPoint espEndpoint;
        private Timer telemetryTimer;
        private CancellationTokenSource shutdown;
        private bool hasTelemetry;

        public PluginManager PluginManager { get; set; }
        public string LeftMenuTitle { get { return "CYD Dashboard"; } }
        public ImageSource PictureIcon { get { return null; } }

        public void Init(PluginManager pluginManager)
        {
            PluginManager = pluginManager;
            settings = PluginSettings.Load();
            espEndpoint = new IPEndPoint(IPAddress.Parse(settings.EspIp), settings.TelemetryPort);
            telemetrySocket = new UdpClient();
            commandSocket = new UdpClient(settings.CommandPort);
            shutdown = new CancellationTokenSource();

            Task.Run(() => ReceiveCommands(shutdown.Token));
            telemetryTimer = new Timer(SendTelemetry, null, 0, 100);
            SimHub.Logging.Current.Info("CYD Dashboard plugin started. Settings: " + PluginSettings.SettingsPath);
        }

        public void DataUpdate(PluginManager pluginManager, ref GameData data)
        {
            if (data == null || !data.GameRunning || data.NewData == null)
            {
                lock (telemetryLock) hasTelemetry = false;
                return;
            }

            double rpm = Math.Max(0, data.NewData.Rpms);
            double speed = Math.Max(0, data.NewData.SpeedKmh);
            object lapValue = data.NewData.CurrentLapTime;
            double lapTime = lapValue is TimeSpan
                ? ((TimeSpan)lapValue).TotalSeconds
                : Convert.ToDouble(lapValue, CultureInfo.InvariantCulture);
            lapTime = Math.Max(0, lapTime);
            int gear = ParseGear(data.NewData.Gear);

            lock (telemetryLock)
            {
                latestTelemetry.Rpm = (long)Math.Round(rpm);
                latestTelemetry.Gear = gear;
                latestTelemetry.Speed = speed;
                latestTelemetry.LapTime = lapTime;
                hasTelemetry = true;
            }
        }

        public void End(PluginManager pluginManager)
        {
            if (shutdown != null) shutdown.Cancel();
            if (telemetryTimer != null) telemetryTimer.Dispose();
            if (commandSocket != null) commandSocket.Close();
            if (telemetrySocket != null) telemetrySocket.Close();
            if (shutdown != null) shutdown.Dispose();
            SimHub.Logging.Current.Info("CYD Dashboard plugin stopped.");
        }

        private void SendTelemetry(object state)
        {
            TelemetrySnapshot snapshot;
            lock (telemetryLock)
            {
                if (!hasTelemetry) return;
                snapshot = latestTelemetry.Clone();
            }

            string json = string.Format(CultureInfo.InvariantCulture,
                "{{\"rpm\":{0},\"gear\":{1},\"speed\":{2:0.0},\"lapTime\":{3:0.000}}}",
                snapshot.Rpm, snapshot.Gear, snapshot.Speed, snapshot.LapTime);
            byte[] bytes = Encoding.UTF8.GetBytes(json);
            try
            {
                telemetrySocket.Send(bytes, bytes.Length, espEndpoint);
            }
            catch (ObjectDisposedException) { }
            catch (SocketException ex)
            {
                SimHub.Logging.Current.Warn("CYD telemetry UDP send failed: " + ex.Message);
            }
        }

        private void ReceiveCommands(CancellationToken token)
        {
            IPEndPoint source = new IPEndPoint(IPAddress.Any, 0);
            while (!token.IsCancellationRequested)
            {
                try
                {
                    byte[] bytes = commandSocket.Receive(ref source);
                    string json = Encoding.UTF8.GetString(bytes);
                    if (json.IndexOf("\"type\":\"" + CommandType + "\"", StringComparison.Ordinal) < 0 &&
                        json.IndexOf("\"type\": \"" + CommandType + "\"", StringComparison.Ordinal) < 0)
                        continue;

                    Match match = CommandRegex.Match(json);
                    if (!match.Success) continue;

                    string command = match.Groups[1].Value;
                    string key = settings.KeyFor(command);
                    if (string.IsNullOrWhiteSpace(key)) continue;
                    KeyPress.Send(key);
                }
                catch (ObjectDisposedException) { break; }
                catch (SocketException) { if (token.IsCancellationRequested) break; }
                catch (Exception ex)
                {
                    SimHub.Logging.Current.Warn("CYD command receive failed: " + ex.Message);
                }
            }
        }

        private static int ParseGear(string gearText)
        {
            if (string.IsNullOrWhiteSpace(gearText)) return 0;
            string gear = gearText.Trim();
            if (gear.Equals("R", StringComparison.OrdinalIgnoreCase)) return -1;
            if (gear.Equals("N", StringComparison.OrdinalIgnoreCase)) return 0;
            int value;
            return int.TryParse(gear, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ? value : 0;
        }

        private sealed class TelemetrySnapshot
        {
            public long Rpm;
            public int Gear;
            public double Speed;
            public double LapTime;

            public TelemetrySnapshot Clone()
            {
                return new TelemetrySnapshot { Rpm = Rpm, Gear = Gear, Speed = Speed, LapTime = LapTime };
            }
        }

        private sealed class PluginSettings
        {
            public string EspIp = "192.168.1.50";
            public int TelemetryPort = 9000;
            public int CommandPort = 9001;
            public string PitLimiterKey = "P";
            public string HeadlightsKey = "H";
            public string WipersKey = "W";
            public string IgnitionKey = "I";
            public string StarterKey = "S";
            public string ResetCarKey = "R";

            public static string SettingsPath
            {
                get
                {
                    return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                        "SimHub", "PluginsData", "CYDDashboard", "settings.ini");
                }
            }

            public static PluginSettings Load()
            {
                PluginSettings result = new PluginSettings();
                string path = SettingsPath;
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                if (!File.Exists(path))
                {
                    File.WriteAllText(path, result.ToIni(), Encoding.UTF8);
                    return result;
                }

                foreach (string line in File.ReadAllLines(path))
                {
                    int separator = line.IndexOf('=');
                    if (separator <= 0) continue;
                    string name = line.Substring(0, separator).Trim();
                    string value = line.Substring(separator + 1).Trim();
                    int port;
                    switch (name.ToLowerInvariant())
                    {
                        case "espip":
                            IPAddress parsed;
                            if (IPAddress.TryParse(value, out parsed)) result.EspIp = parsed.ToString();
                            break;
                        case "telemetryport":
                            if (int.TryParse(value, out port) && port > 0 && port <= 65535) result.TelemetryPort = port;
                            break;
                        case "commandport":
                            if (int.TryParse(value, out port) && port > 0 && port <= 65535) result.CommandPort = port;
                            break;
                        case "pitlimiterkey": result.PitLimiterKey = value; break;
                        case "headlightskey": result.HeadlightsKey = value; break;
                        case "wiperskey": result.WipersKey = value; break;
                        case "ignitionkey": result.IgnitionKey = value; break;
                        case "starterkey": result.StarterKey = value; break;
                        case "resetcarkey": result.ResetCarKey = value; break;
                    }
                }
                return result;
            }

            public string KeyFor(string command)
            {
                switch (command)
                {
                    case "pit_limiter": return PitLimiterKey;
                    case "headlights": return HeadlightsKey;
                    case "wipers": return WipersKey;
                    case "ignition": return IgnitionKey;
                    case "starter": return StarterKey;
                    case "reset_car": return ResetCarKey;
                    default: return null;
                }
            }

            private string ToIni()
            {
                return "# CYD dashboard network and game key bindings\r\n" +
                    "# Set EspIp to the IP printed by the ESP32 serial monitor.\r\n" +
                    "EspIp=" + EspIp + "\r\n" +
                    "TelemetryPort=" + TelemetryPort + "\r\n" +
                    "CommandPort=" + CommandPort + "\r\n" +
                    "PitLimiterKey=" + PitLimiterKey + "\r\n" +
                    "HeadlightsKey=" + HeadlightsKey + "\r\n" +
                    "WipersKey=" + WipersKey + "\r\n" +
                    "IgnitionKey=" + IgnitionKey + "\r\n" +
                    "StarterKey=" + StarterKey + "\r\n" +
                    "ResetCarKey=" + ResetCarKey + "\r\n";
            }
        }

        private static class KeyPress
        {
            private const uint KeyUp = 0x0002;

            [DllImport("user32.dll")]
            private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

            public static void Send(string keyName)
            {
                ushort virtualKey = ParseVirtualKey(keyName);
                if (virtualKey == 0) return;
                keybd_event((byte)virtualKey, 0, 0, UIntPtr.Zero);
                keybd_event((byte)virtualKey, 0, KeyUp, UIntPtr.Zero);
            }

            private static ushort ParseVirtualKey(string keyName)
            {
                if (string.IsNullOrWhiteSpace(keyName)) return 0;
                string key = keyName.Trim().ToUpperInvariant();
                if (key.Length == 1 && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9')))
                    return key[0];
                if (key == "SPACE") return 0x20;
                if (key == "ENTER") return 0x0D;
                if (key == "TAB") return 0x09;
                if (key == "SHIFT") return 0x10;
                if (key == "CTRL" || key == "CONTROL") return 0x11;
                if (key == "ALT") return 0x12;
                if (key.Length >= 2 && key[0] == 'F')
                {
                    int function;
                    if (int.TryParse(key.Substring(1), out function) && function >= 1 && function <= 12)
                        return (ushort)(0x70 + function - 1);
                }
                return 0;
            }
        }
    }
}
