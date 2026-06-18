using System.Buffers.Binary;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using SteamControllerGamepadViewer.State;

namespace SteamControllerGamepadViewer.Hid;

internal sealed class SteamHidTouchpadService : BackgroundService
{
    private const int ValveVendorId = 0x28de;
    private const int HidpStatusSuccess = 0x00110000;
    private const byte TritonControllerState = 0x42;
    private const byte TritonControllerStateNoQuaternion = 0x45;
    private const int SettingLizardMode = 9;
    private const byte SetSettingsValues = 0x87;

    private readonly SteamHidState _state;
    private readonly ILogger<SteamHidTouchpadService> _logger;

    public SteamHidTouchpadService(SteamHidState state, ILogger<SteamHidTouchpadService> logger)
    {
        _state = state;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        while (!stoppingToken.IsCancellationRequested)
        {
            var paths = EnumerateSteamHidPaths().ToArray();
            _state.MarkScan(paths.Length);
            if (paths.Length == 0)
            {
                await Task.Delay(1000, stoppingToken);
                continue;
            }

            var readers = paths
                .Select(path => Task.Run(() => TryReadDeviceAsync(path, stoppingToken), stoppingToken))
                .ToArray();

            await Task.WhenAll(readers);
            await Task.Delay(1000, stoppingToken);
        }
    }

    private async Task<bool> TryReadDeviceAsync(string path, CancellationToken stoppingToken)
    {
        SafeFileHandle handle;
        var canWrite = true;
        try
        {
            handle = WindowsHidNative.CreateFile(
                path,
                WindowsHidNative.GenericRead | WindowsHidNative.GenericWrite,
                WindowsHidNative.FileShareRead | WindowsHidNative.FileShareWrite,
                IntPtr.Zero,
                WindowsHidNative.OpenExisting,
                WindowsHidNative.FileFlagOverlapped,
                IntPtr.Zero);

            if (handle.IsInvalid)
            {
                canWrite = false;
                handle.Dispose();
                handle = WindowsHidNative.CreateFile(
                    path,
                    WindowsHidNative.GenericRead,
                    WindowsHidNative.FileShareRead | WindowsHidNative.FileShareWrite,
                    IntPtr.Zero,
                    WindowsHidNative.OpenExisting,
                    WindowsHidNative.FileFlagOverlapped,
                    IntPtr.Zero);
            }
        }
        catch (Exception ex)
        {
            _logger.LogDebug(ex, "Unable to open Steam HID device {Path}", path);
            _state.MarkError(ex.Message);
            return false;
        }

        if (handle.IsInvalid)
        {
            _state.MarkError($"Unable to open {path}: Win32 error {Marshal.GetLastWin32Error()}");
            handle.Dispose();
            return false;
        }

        var reportLength = GetInputReportLength(handle);
        if (reportLength < 64)
        {
            handle.Dispose();
            return false;
        }

        _logger.LogInformation("Reading Steam Controller raw HID touchpad reports from {Path}", path);
        _state.MarkOpened(path);

        try
        {
            if (canWrite && IsTritonPath(path))
            {
                TryDisableTritonLizardMode(handle);
            }

            await using var stream = new FileStream(handle, FileAccess.Read, reportLength, isAsync: true);
            var buffer = new byte[reportLength];

            while (!stoppingToken.IsCancellationRequested)
            {
                Array.Clear(buffer);
                var read = await stream.ReadAsync(buffer.AsMemory(0, buffer.Length), stoppingToken);
                if (read <= 0)
                {
                    return true;
                }

                _state.MarkReport(read, buffer[0]);

                if (TryParseSnapshot(buffer.AsSpan(0, read), out var snapshot))
                {
                    _state.Update(snapshot);
                }
            }
        }
        catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            _logger.LogDebug(ex, "Steam HID read ended for {Path}", path);
            _state.MarkError(ex.Message);
        }

        return true;
    }

    private static bool IsTritonPath(string path)
        => path.Contains("pid_1302", StringComparison.OrdinalIgnoreCase) ||
           path.Contains("pid_1304", StringComparison.OrdinalIgnoreCase);

    private static void TryDisableTritonLizardMode(SafeFileHandle handle)
    {
        var report = new byte[64];
        report[0] = 1;
        report[1] = SetSettingsValues;
        report[2] = 3;
        report[3] = SettingLizardMode;
        report[4] = 0;
        report[5] = 0;

        WindowsHidNative.HidD_SetFeature(handle, report, report.Length);
    }

    private static int GetInputReportLength(SafeFileHandle handle)
    {
        if (!WindowsHidNative.HidD_GetPreparsedData(handle, out var preparsedData))
        {
            return 64;
        }

        try
        {
            return WindowsHidNative.HidP_GetCaps(preparsedData, out var caps) == HidpStatusSuccess
                ? Math.Max(64, (int)caps.InputReportByteLength)
                : 64;
        }
        finally
        {
            WindowsHidNative.HidD_FreePreparsedData(preparsedData);
        }
    }

    private static IEnumerable<string> EnumerateSteamHidPaths()
    {
        WindowsHidNative.HidD_GetHidGuid(out var hidGuid);
        var deviceInfoSet = WindowsHidNative.SetupDiGetClassDevs(
            ref hidGuid,
            IntPtr.Zero,
            IntPtr.Zero,
            WindowsHidNative.DigcfPresent | WindowsHidNative.DigcfDeviceInterface);

        if (deviceInfoSet == new IntPtr(WindowsHidNative.InvalidHandleValue))
        {
            yield break;
        }

        try
        {
            for (uint index = 0; ; index++)
            {
                var interfaceData = new WindowsHidNative.SpDeviceInterfaceData
                {
                    CbSize = Marshal.SizeOf<WindowsHidNative.SpDeviceInterfaceData>(),
                };

                if (!WindowsHidNative.SetupDiEnumDeviceInterfaces(
                        deviceInfoSet,
                        IntPtr.Zero,
                        ref hidGuid,
                        index,
                        ref interfaceData))
                {
                    yield break;
                }

                WindowsHidNative.SetupDiGetDeviceInterfaceDetail(
                    deviceInfoSet,
                    ref interfaceData,
                    IntPtr.Zero,
                    0,
                    out var requiredSize,
                    IntPtr.Zero);

                if (requiredSize <= 0)
                {
                    continue;
                }

                var detailData = Marshal.AllocHGlobal(requiredSize);
                try
                {
                    Marshal.WriteInt32(detailData, IntPtr.Size == 8 ? 8 : 6);
                    if (!WindowsHidNative.SetupDiGetDeviceInterfaceDetail(
                            deviceInfoSet,
                            ref interfaceData,
                            detailData,
                            requiredSize,
                            out _,
                            IntPtr.Zero))
                    {
                        continue;
                    }

                    var path = ReadDevicePath(detailData);
                    if (path.Contains($"vid_{ValveVendorId:x4}", StringComparison.OrdinalIgnoreCase))
                    {
                        yield return path;
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(detailData);
                }
            }
        }
        finally
        {
            WindowsHidNative.SetupDiDestroyDeviceInfoList(deviceInfoSet);
        }
    }

    private static string ReadDevicePath(IntPtr detailData)
    {
        foreach (var offset in new[] { 4, 8, 6 })
        {
            var path = Marshal.PtrToStringUni(IntPtr.Add(detailData, offset));
            if (!string.IsNullOrWhiteSpace(path) && path.StartsWith(@"\\?\", StringComparison.Ordinal))
            {
                return path;
            }
        }

        return string.Empty;
    }

    private static bool TryParseSnapshot(
        ReadOnlySpan<byte> report,
        out SteamHidSnapshot snapshot)
    {
        snapshot = default;

        if (TryParseTritonState(report, out snapshot))
        {
            return true;
        }

        for (var offset = 0; offset <= Math.Min(4, report.Length - 24); offset++)
        {
            if (report[offset] != 0x01 || report[offset + 1] != 0x00)
            {
                continue;
            }

            var data = report[offset..];
            var left = new ControllerTouchpad();
            var right = new ControllerTouchpad();
            var parsed = data[2] switch
            {
                0x01 => TryParseClassicState(data, out left, out right),
                0x09 => TryParseDeckState(data, out left, out right),
                _ => false,
            };

            if (parsed)
            {
                snapshot = new SteamHidSnapshot(null, null, null, left, right, DateTimeOffset.UtcNow);
                return true;
            }
        }

        return false;
    }

    private static bool TryParseTritonState(ReadOnlySpan<byte> report, out SteamHidSnapshot snapshot)
    {
        snapshot = default;

        if (report.Length < 30 ||
            (report[0] != TritonControllerState && report[0] != TritonControllerStateNoQuaternion))
        {
            return false;
        }

        var buttons = BinaryPrimitives.ReadUInt32LittleEndian(report[2..6]);
        var leftPressure = NormalizePressure(BinaryPrimitives.ReadUInt16LittleEndian(report[22..24]));
        var rightPressure = NormalizePressure(BinaryPrimitives.ReadUInt16LittleEndian(report[28..30]));
        var left = CreatePad(
            HasFlag(buttons, 0x02000000),
            HasFlag(buttons, 0x04000000),
            ReadSigned(report, 18),
            (short)-ReadSigned(report, 20),
            leftPressure);
        var right = CreatePad(
            HasFlag(buttons, 0x00200000),
            HasFlag(buttons, 0x00400000),
            ReadSigned(report, 24),
            (short)-ReadSigned(report, 26),
            rightPressure);

        snapshot = new SteamHidSnapshot(
            buttons,
            new ControllerButtons
            {
                A = HasFlag(buttons, 0x00000001),
                B = HasFlag(buttons, 0x00000002),
                X = HasFlag(buttons, 0x00000004),
                Y = HasFlag(buttons, 0x00000008),
                QuickAccess = HasFlag(buttons, 0x00000010),
                RightStick = HasFlag(buttons, 0x00000020),
                LeftStickTouch = HasFlag(buttons, 0x01000000),
                RightStickTouch = HasFlag(buttons, 0x00100000),
                View = HasFlag(buttons, 0x00004000),
                RightGripUpper = HasFlag(buttons, 0x00000080),
                RightGripLower = HasFlag(buttons, 0x00000100),
                RightBumper = HasFlag(buttons, 0x00000200),
                DpadDown = HasFlag(buttons, 0x00000400),
                DpadRight = HasFlag(buttons, 0x00000800),
                DpadLeft = HasFlag(buttons, 0x00001000),
                DpadUp = HasFlag(buttons, 0x00002000),
                Menu = HasFlag(buttons, 0x00000040),
                LeftStick = HasFlag(buttons, 0x00008000),
                Steam = HasFlag(buttons, 0x00010000),
                LeftGripUpper = HasFlag(buttons, 0x00020000),
                LeftGripLower = HasFlag(buttons, 0x00040000),
                LeftBumper = HasFlag(buttons, 0x00080000),
                RightGripTouch = HasFlag(buttons, 0x10000000),
                LeftGripTouch = HasFlag(buttons, 0x20000000),
            },
            new ControllerAxes
            {
                LeftTrigger = NormalizeTrigger(BinaryPrimitives.ReadUInt16LittleEndian(report[6..8])),
                RightTrigger = NormalizeTrigger(BinaryPrimitives.ReadUInt16LittleEndian(report[8..10])),
                LeftStickX = NormalizeAxis(ReadSigned(report, 10)),
                LeftStickY = NormalizeAxis((short)-ReadSigned(report, 12)),
                RightStickX = NormalizeAxis(ReadSigned(report, 14)),
                RightStickY = NormalizeAxis((short)-ReadSigned(report, 16)),
            },
            left,
            right,
            DateTimeOffset.UtcNow);
        return true;
    }

    private static bool TryParseClassicState(
        ReadOnlySpan<byte> data,
        out ControllerTouchpad left,
        out ControllerTouchpad right)
    {
        left = new ControllerTouchpad();
        right = new ControllerTouchpad();

        if (data.Length < 24)
        {
            return false;
        }

        var b10 = data[10];
        var leftTouched = HasBit(b10, 3) || HasBit(b10, 7);
        var rightTouched = HasBit(b10, 4);
        var leftClicked = HasBit(b10, 1);
        var rightClicked = HasBit(b10, 2);

        left = CreatePad(
            leftTouched,
            leftClicked,
            ReadSigned(data, 16),
            (short)-ReadSigned(data, 18),
            leftClicked ? 1 : leftTouched ? 0.35 : 0);

        right = CreatePad(
            rightTouched,
            rightClicked,
            ReadSigned(data, 20),
            (short)-ReadSigned(data, 22),
            rightClicked ? 1 : rightTouched ? 0.35 : 0);

        return true;
    }

    private static bool TryParseDeckState(
        ReadOnlySpan<byte> data,
        out ControllerTouchpad left,
        out ControllerTouchpad right)
    {
        left = new ControllerTouchpad();
        right = new ControllerTouchpad();

        if (data.Length < 60)
        {
            return false;
        }

        var b10 = data[10];
        var leftPressure = NormalizePressure(BinaryPrimitives.ReadUInt16LittleEndian(data[56..58]));
        var rightPressure = NormalizePressure(BinaryPrimitives.ReadUInt16LittleEndian(data[58..60]));
        var leftTouched = HasBit(b10, 3) || leftPressure > 0.02;
        var rightTouched = HasBit(b10, 4) || rightPressure > 0.02;
        var leftClicked = HasBit(b10, 1);
        var rightClicked = HasBit(b10, 2);

        left = CreatePad(
            leftTouched,
            leftClicked,
            ReadSigned(data, 16),
            ReadSigned(data, 18),
            leftClicked ? 1 : leftTouched ? Math.Max(0.35, leftPressure) : 0);

        right = CreatePad(
            rightTouched,
            rightClicked,
            ReadSigned(data, 20),
            ReadSigned(data, 22),
            rightClicked ? 1 : rightTouched ? Math.Max(0.35, rightPressure) : 0);

        return true;
    }

    private static ControllerTouchpad CreatePad(bool touched, bool clicked, short x, short y, double pressure)
        => new()
        {
            Touched = touched,
            Clicked = clicked,
            X = NormalizeSigned(x),
            Y = NormalizeSigned(y),
            Pressure = Clamp01(pressure),
        };

    private static short ReadSigned(ReadOnlySpan<byte> data, int offset)
        => BinaryPrimitives.ReadInt16LittleEndian(data[offset..(offset + 2)]);

    private static bool HasBit(byte value, int bit)
        => (value & (1 << bit)) != 0;

    private static bool HasFlag(uint value, uint flag)
        => (value & flag) != 0;

    private static double NormalizeAxis(short value)
    {
        var divisor = value < 0 ? 32768d : 32767d;
        return Math.Clamp(value / divisor, -1d, 1d);
    }

    private static double NormalizeSigned(short value)
        => Clamp01((value + 32767d) / 65534d);

    private static double NormalizeTrigger(ushort value)
        => Clamp01(value / 32767d);

    private static double NormalizePressure(ushort value)
        => Clamp01(value / 32767d);

    private static double Clamp01(double value)
        => Math.Min(1, Math.Max(0, value));
}
