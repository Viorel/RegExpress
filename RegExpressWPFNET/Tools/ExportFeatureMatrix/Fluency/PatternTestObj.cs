using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix.Fluency;

class PatternTestObj : BoolStep
{
    readonly Func<ExecContext, bool>? precondition;
    readonly string pattern;
    readonly string? textMatch;
    readonly string? textNoMatch;
    readonly string? expected;

    public PatternTestObj( Func<ExecContext, bool>? precondition, string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        this.precondition = precondition;
        this.pattern = pattern;
        this.textMatch = textMatch;
        this.textNoMatch = textNoMatch;
        this.expected = expected;
    }

    public override bool Exec( ExecContext context )
    {
        if( precondition != null && !precondition( context ) ) return false;

        return Try( context, pattern, textMatch, textNoMatch, expected );
    }

    public static bool Try( ExecContext context, string pattern, string? textMatch, string? textNoMatch, string? expected )
    {
        RegexEngine engine = context.Engine;
        FeatureMatrix fm = context.Fm;

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
