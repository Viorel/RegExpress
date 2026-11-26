namespace RegExpressLibrary.Matches.Simple;

public sealed class SimpleCapture : SimpleBase, ICapture
{
    internal SimpleCapture( Coordinates coordinates, ISimpleTextGetter textGetter )
        : base( coordinates, textGetter )
    {
    }
}
