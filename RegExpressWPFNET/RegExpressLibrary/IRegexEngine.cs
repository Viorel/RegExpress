using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Controls;
using RegExpressLibrary.Matches;


namespace RegExpressLibrary
{

    public delegate void RegexEngineOptionsChanged( IRegexEngine sender, RegexEngineOptionsChangedArgs args );


    public interface IRegexEngine
    {
        string Kind { get; }

        Version Version { get; }

        string Name { get; }


        (string Kind, Version Version) CombinedId => (Kind, Version);


        RegexEngineCapabilityEnum Capabilities { get; }

        string? NoteForCaptures { get; }

        event RegexEngineOptionsChanged? OptionsChanged;

        Control GetOptionsControl( );

        string? ExportOptions( ); // (json)

        void ImportOptions( string? json );

        IMatcher ParsePattern( string pattern );

        FeatureMatrix GetFeatureMatrix( );

        GenericOptions GetGenericOptions( );

        IReadOnlyList<(string variantName, FeatureMatrix fm)> GetFeatureMatrices( );

    }
}
