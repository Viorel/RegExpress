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

    public interface IBaseEngine
    {
        string Name { get; }
        string? ExportOptions( ); // (JSON)
    }
    public interface IAIBase : IBaseEngine
    {
        string AIPatternType => "Regex";
        string AIPatternCodeblockType => "regex";
        string AIAdditionalSystemPrompt => "If the language supports named capture groups, use these by default. " +
                   "If the user has ignoring patterned whitespace enabled in the options, use multi-lines and minimal in-regex comments for complex regexes with nice whitespace formatting to make it more readable. ";

        string ReferenceTextHeader => "Users current target text";
        string GetSystemPrompt( ) => $"You are a {Name} {AIPatternType} expert assistant. The user has questions about their {AIPatternType} patterns and target text. " +
                   $"Provide {AIPatternType} patterns inside Markdown code blocks (```{AIPatternCodeblockType} ... ```). " +
                   "Explain how the pattern works briefly. " +
                    AIAdditionalSystemPrompt +
                   $"They currently have these engine options enabled:\n```json\n{ExportOptions( )}\n```";
    }

    public interface IRegexEngine : IAIBase
    {
        event RegexEngineOptionsChanged? OptionsChanged;
        event EventHandler? FeatureMatrixReady;

        string Kind { get; }

        string? Version { get; }

        (string Kind, string? Version) CombinedId => (Kind, Version);

        string Subtitle { get; }

        RegexEngineCapabilityEnum Capabilities { get; }

        string? NoteForCaptures { get; }

        Control GetOptionsControl( );

        void ImportOptions( string? json );

        RegexMatches GetMatches( ICancellable cnc, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string text );

        SyntaxOptions GetSyntaxOptions( );


        // For additional tools

        IReadOnlyList<FeatureMatrixVariant> GetFeatureMatrices( );
        void SetIgnoreCase( bool yes ); // (if supported)
        void SetIgnorePatternWhitespace( bool yes ); // (if supported)
    }
}
