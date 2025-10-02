using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;


namespace RegExpressLibrary
{

    public delegate void RegexEngineOptionsChanged( IRegexEngine sender, RegexEngineOptionsChangedArgs args );


    public interface IRegexEngine
    {
        event RegexEngineOptionsChanged? OptionsChanged;
        event EventHandler? FeatureMatrixReady;

        string Kind { get; }

        string? Version { get; }

        (string Kind, string? Version) CombinedId => (Kind, Version);

        string Name { get; }

        string Subtitle { get; }

        RegexEngineCapabilityEnum Capabilities { get; }

        string? NoteForCaptures { get; }

        Control GetOptionsControl( );

        string? ExportOptions( ); // (JSON)

        void ImportOptions( string? json );

        RegexMatches GetMatches( ICancellable cnc, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string text );

        SyntaxOptions GetSyntaxOptions( );

        IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( ); // should include "Ignore pattern whitespace"
    }
}
