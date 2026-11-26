using System;
using System.Diagnostics;


namespace RegExpressLibrary.Matches.Simple;

public abstract class SimpleBase
{
    public Coordinates Coordinates { get; }
    protected readonly ISimpleTextGetter TextGetter;

    protected SimpleBase( Coordinates coordinates, ISimpleTextGetter textGetter )
    {
        Debug.Assert( textGetter != null );

        this.Coordinates = coordinates;
        this.TextGetter = textGetter;
    }

    public string Value => Coordinates.CharIndex < 0 ? String.Empty : TextGetter.GetText( Coordinates.CharIndex, Coordinates.CharLength );
}
