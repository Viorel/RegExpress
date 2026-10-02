using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using System.Diagnostics.CodeAnalysis;


namespace RegExpressLibrary;

public abstract class RegexSubengine
{
    public abstract RegexEngineCapabilityEnum GetCapabilities( );
    public abstract SyntaxOptions GetSyntaxOptions( );
    public abstract RegexMatches GetMatches( ICancellable cnc, [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string text );
}
