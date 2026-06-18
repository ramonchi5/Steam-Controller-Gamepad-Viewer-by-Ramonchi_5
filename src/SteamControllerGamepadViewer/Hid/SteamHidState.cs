using SteamControllerGamepadViewer.State;

namespace SteamControllerGamepadViewer.Hid;

internal sealed class SteamHidState
{
    private readonly object _gate = new();
    private SteamHidSnapshot? _current;
    private SteamHidStatus _status = new();

    public void MarkReport(int length, byte reportId)
    {
        lock (_gate)
        {
            _status = _status with
            {
                ReportsSeen = _status.ReportsSeen + 1,
                LastReportLength = length,
                LastReportId = reportId,
            };
        }
    }

    public void Update(SteamHidSnapshot snapshot)
    {
        lock (_gate)
        {
            _current = snapshot with { UpdatedAt = DateTimeOffset.UtcNow };
            _status = _status with
            {
                LastReportAt = DateTimeOffset.UtcNow,
                ReportsParsed = _status.ReportsParsed + 1,
                LastRawButtons = snapshot.RawButtons,
                LastButtons = snapshot.Buttons,
                LastAxes = snapshot.Axes,
                LastLeft = snapshot.Left,
                LastRight = snapshot.Right,
                LastError = null,
            };
        }
    }

    public void MarkScan(int devicesFound)
    {
        lock (_gate)
        {
            _status = _status with
            {
                DevicesFound = devicesFound,
                LastScanAt = DateTimeOffset.UtcNow,
            };
        }
    }

    public void MarkOpened(string path)
    {
        lock (_gate)
        {
            _status = _status with
            {
                OpenedPath = path,
                LastOpenAt = DateTimeOffset.UtcNow,
                LastError = null,
            };
        }
    }

    public void MarkError(string error)
    {
        lock (_gate)
        {
            _status = _status with
            {
                LastError = error,
            };
        }
    }

    public bool TryGetCurrent(TimeSpan maxAge, out SteamHidSnapshot snapshot)
    {
        lock (_gate)
        {
            if (_current is { } current && DateTimeOffset.UtcNow - current.UpdatedAt <= maxAge)
            {
                snapshot = current;
                return true;
            }
        }

        snapshot = default;
        return false;
    }

    public SteamHidStatus Status
    {
        get
        {
            lock (_gate)
            {
                return _status;
            }
        }
    }
}

internal readonly record struct SteamHidSnapshot(
    uint? RawButtons,
    ControllerButtons? Buttons,
    ControllerAxes? Axes,
    ControllerTouchpad Left,
    ControllerTouchpad Right,
    DateTimeOffset UpdatedAt);

internal sealed record SteamHidStatus
{
    public int DevicesFound { get; init; }
    public string? OpenedPath { get; init; }
    public DateTimeOffset? LastScanAt { get; init; }
    public DateTimeOffset? LastOpenAt { get; init; }
    public long ReportsSeen { get; init; }
    public int LastReportLength { get; init; }
    public byte LastReportId { get; init; }
    public DateTimeOffset? LastReportAt { get; init; }
    public uint? LastRawButtons { get; init; }
    public ControllerButtons? LastButtons { get; init; }
    public ControllerAxes? LastAxes { get; init; }
    public ControllerTouchpad? LastLeft { get; init; }
    public ControllerTouchpad? LastRight { get; init; }
    public long ReportsParsed { get; init; }
    public string? LastError { get; init; }
}
