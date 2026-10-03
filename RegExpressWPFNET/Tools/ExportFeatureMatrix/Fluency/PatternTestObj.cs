using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class PatternTestObj : BoolStep
{
    readonly string pattern;
    readonly string? textMatch;
    readonly string? textNoMatch;
    readonly string? expected;

    public PatternTestObj( string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        this.pattern = pattern;
        this.textMatch = textMatch;
        this.textNoMatch = textNoMatch;
        this.expected = expected;
    }

    public override bool Exec( RegexEngine engine, FeatureMatrix fm, ExecContext execContext )
    {
        bool match_satisfied = false;
        bool nomatch_satisfied = false;

        if( textMatch != null )
        {
            try
            {
                RegexMatches? resultMatch = engine.GetMatches( ICancellable.NonCancellable, pattern, textMatch );

                if( resultMatch.Count > 0 )
                {
                    if( expected == null )
                    {
                        match_satisfied = true;
                    }
                    else
                    {
                        match_satisfied = resultMatch.Matches.First( ).Value == expected;
                    }
                }
            }
            catch
            {
                // ignore
            }
        }

        if( textNoMatch != null )
        {
            try
            {
                RegexMatches? resultNoMatch = engine.GetMatches( ICancellable.NonCancellable, pattern, textNoMatch );

                nomatch_satisfied = resultNoMatch.Count == 0;
            }
            catch
            {
                // ignore

                nomatch_satisfied = true;
            }
        }

        if( ( textMatch != null && match_satisfied && textNoMatch != null && nomatch_satisfied ) ||
            ( textMatch != null && match_satisfied && textNoMatch == null ) ||
            ( textMatch == null && textNoMatch != null && nomatch_satisfied ) )
        {
            return true;
        }

        return false;
    }
}
