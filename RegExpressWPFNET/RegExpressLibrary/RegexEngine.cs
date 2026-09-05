using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using System;
using System.Collections.Generic;
using System.Windows.Controls;


namespace RegExpressLibrary;

public delegate void RegexEngineOptionsChanged( RegexEngine sender, RegexEngineOptionsChangedArgs args );

public abstract class RegexEngine
{
    public abstract string Kind { get; }
    public abstract string Version { get; }
    public abstract string Name { get; }
    public abstract string Subtitle { get; }

    public abstract RegexSubengine GetSubengine( );

    public virtual string? NoteForCaptures => null;

    public event RegexEngineOptionsChanged? OptionsChanged;
    public event EventHandler? FeatureMatrixReady;

    public abstract Control GetOptionsControl( );
    public abstract string? ExportOptions( ); // (JSON)
    public abstract void ImportOptions( string? json );

    public abstract IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( );

    public abstract void SetIgnoreCase( bool yes );
    public abstract void SetIgnorePatternWhitespace( bool yes );
    public abstract void SetCollectCaptures( bool yes );


    public RegexEngineCapabilityEnum Capabilities => GetSubengine( ).GetCapabilities( );
    public SyntaxOptions GetSyntaxOptions( ) => GetSubengine( ).GetSyntaxOptions( );
    public RegexMatches GetMatches( ICancellable cnc, string pattern, string text ) => GetSubengine( ).GetMatches( cnc, pattern, text );


    protected void InvokeOptionsChanged( RegexEngineOptionsChangedArgs args )
    {
        OptionsChanged?.Invoke( this, args );
    }

    protected void InvokeFeatureMatrixReady( )
    {
        FeatureMatrixReady?.Invoke( this, EventArgs.Empty );
    }
}
