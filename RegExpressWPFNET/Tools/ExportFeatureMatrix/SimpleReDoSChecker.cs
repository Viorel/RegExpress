using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

internal static class SimpleReDosChecker
{
    internal enum CatastrophicBacktrackingResultEnum
    {
        None,
        Passed,
        Timeout,
        Error,
        Unknown,
    }

    internal static CatastrophicBacktrackingResultEnum CheckCatastrophicPattern( RegexEngine engine, FeatureMatrix fm )
    {
        try
        {
            SimpleCancellable cnc = new( );

            CatastrophicBacktrackingResultEnum result = CatastrophicBacktrackingResultEnum.None;

            var t = new Thread( ( ) =>
            {
                try
                {
                    const string TEXT = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac";

                    switch( fm.Parentheses )
                    {
                    case FeatureMatrix.PunctuationEnum.Normal:
                    {
                        var unused1 = engine.GetMatches( cnc, @"(a*)*b", TEXT ).Matches.FirstOrDefault( );
                        var unused2 = engine.GetMatches( cnc, @"(a+)+b", TEXT ).Matches.FirstOrDefault( ); ;
                        result = CatastrophicBacktrackingResultEnum.Passed;
                        break;
                    }
                    case FeatureMatrix.PunctuationEnum.Backslashed:
                    {
                        var unused1 = engine.GetMatches( cnc, @"\(a*\)*b", TEXT ).Matches.FirstOrDefault( );
                        var unused2 = engine.GetMatches( cnc, @"\(a+\)+b", TEXT ).Matches.FirstOrDefault( );
                        result = CatastrophicBacktrackingResultEnum.Passed;
                        break;
                    }
                    default:
                        result = CatastrophicBacktrackingResultEnum.Unknown;
                        break;
                    }

                    return;
                }
                catch( ThreadInterruptedException )
                {
                    return;
                }
                catch( Exception exc )
                {
                    _ = exc;
                    // ...

                    result = CatastrophicBacktrackingResultEnum.Error;

                    return;
                }
            } )
            {
                IsBackground = true
            };

            t.SetApartmentState( ApartmentState.STA );
            t.Start( );

            bool no_timeout = t.Join( 4444 );
            cnc.SetCancel( );

            if( !no_timeout )
            {
                t.Join( 1111 );
                t.Interrupt( );
                t.Join( 1111 );
            }

            return no_timeout ? result : CatastrophicBacktrackingResultEnum.Timeout;
        }
        catch( Exception exc )
        {
            _ = exc;

            //...

            return CatastrophicBacktrackingResultEnum.None;
        }
    }
}
