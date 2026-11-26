using System.Collections.Generic;


namespace RegExpressLibrary.Matches.Simple;

public sealed class SimpleGroup : SimpleBase, IGroup
{
    readonly List<ICapture> mCaptures = [];


    internal SimpleGroup( Coordinates coordinates, bool success, string name, ISimpleTextGetter textGetter )
        : base( coordinates, textGetter )
    {
        Success = success;
        Name = name;
    }


    #region IGroup

    public bool Success { get; }

    public string Name { get; private set; }

    public bool ValueOnly { get; private set; } = false;

    public IEnumerable<ICapture> Captures => mCaptures;

    #endregion IGroup

    public SimpleCapture AddCapture( Coordinates coordinates )
    {
        TextGetter.ThrowIfInvalid( coordinates.CharIndex, coordinates.CharLength );

        var capture = new SimpleCapture( coordinates, TextGetter );
        mCaptures.Add( capture );

        return capture;
    }

    public void SetName( string name )
    {
        Name = name;
    }

    public void SetValueOnly( )
    {
        ValueOnly = true;
    }
}
