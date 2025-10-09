using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using DocumentFormat.OpenXml.Bibliography;
using RegExpressLibrary;
using RegExpressLibrary.SyntaxColouring;

namespace ExportFeatureMatrix;

class FeatureMatrixGroup
{
    public string Name { get; }
    public IReadOnlyCollection<FeatureMatrixDetails> Details { get; }

    public FeatureMatrixGroup( string name, IReadOnlyCollection<FeatureMatrixDetails> details )
    {
        Name = name;
        Details = details;
    }
}

partial class FeatureMatrixDetails
{
    internal class Rule
    {
        internal string? Pattern { get; }
        internal string? TextToMatch { get; }
        internal string? TextToNotMatch { get; }
        internal bool IgnoreCase { get; }
        internal bool IgnorePatternWhitespace { get; }
        internal Func<IRegexEngine, FeatureMatrix, bool>? DirectCheck { get; }

        internal Rule( string pattern, string? textToMatch, string? textToNotMatch, bool ignoreCase, bool ignorePatternWhitespace )
        {
            Debug.Assert( textToMatch != null || textToNotMatch != null );

            Pattern = pattern;
            TextToMatch = textToMatch;
            TextToNotMatch = textToNotMatch;
            IgnoreCase = ignoreCase;
            IgnorePatternWhitespace = ignorePatternWhitespace;
            DirectCheck = null;
        }

        internal Rule( Func<IRegexEngine, FeatureMatrix, bool> directCheck, bool ignoreCase, bool ignorePatternWhitespace )
        {
            Pattern = null;
            TextToMatch = null;
            TextToNotMatch = null;
            IgnoreCase = ignoreCase;
            IgnorePatternWhitespace = ignorePatternWhitespace;
            DirectCheck = directCheck;
        }
    }

    internal readonly string ShortDesc;
    internal readonly string? Desc;
    internal readonly Func<FeatureMatrix, bool> ValueGetter;
    internal readonly List<Rule> Rules = [];
    bool mIgnoreCase = false;
    bool mIgnorePatternWhitespace = false;

    internal FeatureMatrixDetails( string shortDesc, string desc, Func<FeatureMatrix, bool> valueGetter )
    {
        ShortDesc = shortDesc;
        Desc = desc;
        ValueGetter = valueGetter;
    }

    FeatureMatrixDetails IgnoreCase( bool yes = true )
    {
        mIgnoreCase = yes;

        return this;
    }

    FeatureMatrixDetails IgnorePatternWhitespace( bool yes = true )
    {
        mIgnorePatternWhitespace = yes;

        return this;
    }

    FeatureMatrixDetails Test( [StringSyntax( StringSyntaxAttribute.Regex )] string pattern, string? textMatch, string? textNoMatch )
    {
        Rules.Add( new Rule( pattern, textMatch, textNoMatch, mIgnoreCase, mIgnorePatternWhitespace ) );

        return this;
    }

    FeatureMatrixDetails Test( Func<IRegexEngine, FeatureMatrix, bool> func )
    {
        Rules.Add( new Rule( func, mIgnoreCase, mIgnorePatternWhitespace ) );

        return this;
    }

    enum CatastrophicBacktrackingResultEnum
    {
        None,
        Passed,
        Timeout,
        Error,
    }

    static CatastrophicBacktrackingResultEnum CheckCatastrophicPattern( IRegexEngine engine, FeatureMatrix fm )
    {
        try
        {
            SimpleCancellable cnc = new( );

            CatastrophicBacktrackingResultEnum result = CatastrophicBacktrackingResultEnum.None;

            var t = new Thread( ( ) =>
            {
                try
                {
                    switch( fm.Parentheses )
                    {
                    case FeatureMatrix.PunctuationEnum.Normal:
                    {
                        var _ = engine.GetMatches( cnc, @"(a*)*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac" );
                        result = CatastrophicBacktrackingResultEnum.Passed;
                        break;
                    }
                    case FeatureMatrix.PunctuationEnum.Backslashed:
                    {
                        var _ = engine.GetMatches( cnc, @"\(a*\)*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac" );
                        result = CatastrophicBacktrackingResultEnum.Passed;
                        break;
                    }
                    default:
                        result = CatastrophicBacktrackingResultEnum.Error;
                        break;
                    }

                    return;
                }
                catch( ThreadInterruptedException )
                {
                    return;
                }
                catch
                {
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
