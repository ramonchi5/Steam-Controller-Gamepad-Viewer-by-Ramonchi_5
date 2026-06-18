namespace SteamControllerGamepadViewer.State;

public sealed record ControllerSnapshot
{
    public long Version { get; init; }
    public bool Connected { get; init; }
    public string Status { get; init; } = "starting";
    public string Name { get; init; } = "Steam Controller";
    public int InstanceId { get; init; }
    public int VendorId { get; init; }
    public int ProductId { get; init; }
    public DateTimeOffset UpdatedAt { get; init; } = DateTimeOffset.UtcNow;
    public ControllerButtons Buttons { get; init; } = new();
    public ControllerAxes Axes { get; init; } = new();
    public ControllerTouchpad LeftTouchpad { get; init; } = new();
    public ControllerTouchpad RightTouchpad { get; init; } = new();

    public static ControllerSnapshot Starting() => new()
    {
        Status = "starting",
        Connected = false,
        Name = "Steam Controller",
    };
}

public sealed record ControllerButtons
{
    public bool A { get; init; }
    public bool B { get; init; }
    public bool X { get; init; }
    public bool Y { get; init; }
    public bool View { get; init; }
    public bool Menu { get; init; }
    public bool Steam { get; init; }
    public bool QuickAccess { get; init; }
    public bool LeftBumper { get; init; }
    public bool RightBumper { get; init; }
    public bool LeftStick { get; init; }
    public bool RightStick { get; init; }
    public bool LeftStickTouch { get; init; } = true;
    public bool RightStickTouch { get; init; } = true;
    public bool DpadUp { get; init; }
    public bool DpadDown { get; init; }
    public bool DpadLeft { get; init; }
    public bool DpadRight { get; init; }
    public bool LeftGripUpper { get; init; }
    public bool LeftGripLower { get; init; }
    public bool RightGripUpper { get; init; }
    public bool RightGripLower { get; init; }
    public bool LeftGripTouch { get; init; }
    public bool RightGripTouch { get; init; }
}

public sealed record ControllerAxes
{
    public double LeftStickX { get; init; }
    public double LeftStickY { get; init; }
    public double RightStickX { get; init; }
    public double RightStickY { get; init; }
    public double LeftTrigger { get; init; }
    public double RightTrigger { get; init; }
}

public sealed record ControllerTouchpad
{
    public bool Touched { get; init; }
    public bool Clicked { get; init; }
    public double X { get; init; } = 0.5;
    public double Y { get; init; } = 0.5;
    public double Pressure { get; init; }
}
