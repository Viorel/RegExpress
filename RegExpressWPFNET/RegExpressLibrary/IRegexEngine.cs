using System;
using System.Collections.Generic;
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

        Version Version { get; }

        (string Kind, Version Version) CombinedId => (Kind, Version);

        string Name { get; }

        RegexEngineCapabilityEnum Capabilities { get; }

        string? NoteForCaptures { get; }

        Control GetOptionsControl( );

        string? ExportOptions( ); // (JSON)

        void ImportOptions( string? json );

        IMatcher ParsePattern( string pattern );

        SyntaxOptions GetSyntaxOptions( );

    }
}
