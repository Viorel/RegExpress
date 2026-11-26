using System;
using System.Collections.Generic;


namespace RegExpressLibrary.Matches.Simple;

public sealed class SimpleMatch : SimpleBase, IMatch
{
    readonly List<IGroup> mGroups = [];

    private SimpleMatch( Coordinates coordinates, ISimpleTextGetter textGetter )
        : base( coordinates, textGetter )
    {
    }

    public static SimpleMatch Create( Coordinates coordinates, ISimpleTextGetter textGetter )
    {
        textGetter.ThrowIfInvalid( coordinates.CharIndex, coordinates.CharLength );

        return new SimpleMatch( coordinates, textGetter );
    }


    #region IMatch

    public IEnumerable<IGroup> Groups => mGroups;

    public bool Success { get; } = true; // TODO: reconsider the inheritance

    public string Name { get; } = ""; // TODO: reconsider the inheritance

    #endregion IMatch


    #region IGroup

    public IEnumerable<ICapture> Captures
    {
        get
        {
            // Not expected to be called.

            throw new InvalidOperationException( );
        }
    }

    public bool ValueOnly
    {
        get
        {
            // Not expected to be called.

            throw new InvalidOperationException( );
        }
    }

    #endregion IGroup

    public SimpleGroup AddDefaultGroup( )
    {
        return AddSucceededGroup( Coordinates, "" );
    }

    public SimpleGroup AddFailedGroup( string name )
    {
        SimpleGroup group = new( new Coordinates( 0, 0, 0, 0 ), false, name, TextGetter );
        mGroups.Add( group );

        return group;
    }

    public SimpleGroup AddSucceededGroup( Coordinates coordinates, string name )
    {
        TextGetter.ThrowIfInvalid( coordinates.CharIndex, coordinates.CharLength );

        SimpleGroup group = new( coordinates, true, name, TextGetter );
        mGroups.Add( group );

        return group;
    }

    public SimpleGroup AddSucceededNoDetailsGroup( string name, string value )
    {
        SimpleGroup group = new( new Coordinates( Coordinates.NativeIndex, Coordinates.NativeLength, Coordinates.CharIndex, value.Length ), true, name, new TrivialTextGetter( value ) );
        group.SetValueOnly( );
        mGroups.Add( group );

        return group;
    }

    public void SetGroupName( int index, string name )
    {
        ( (SimpleGroup)mGroups[index] ).SetName( name );
    }
}
