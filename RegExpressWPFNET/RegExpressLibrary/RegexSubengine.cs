using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;


namespace RegExpressLibrary;

public abstract class RegexSubengine
{
    public abstract RegexEngineCapabilityEnum GetCapabilities( );
    public abstract SyntaxOptions GetSyntaxOptions( );
    public abstract RegexMatches GetMatches( ICancellable cnc, string pattern, string text );
}
