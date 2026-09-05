namespace RegExpressLibrary.Matches
{
    public interface ICapture
    {
        int NativeIndex { get; }

        int NativeLength { get; }


        int CharIndex { get; }

        int CharLength { get; }


        string Value { get; }
    }

}
