namespace RegExpressLibrary.Matches;

public interface ICapture
{
    Coordinates Coordinates { get; }

    public int CharIndex => Coordinates.CharIndex;
    public int CharLength => Coordinates.CharLength;

    string Value { get; }
}
