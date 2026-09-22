using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;


namespace RegExpressLibrary.Matches
{
    public sealed class RegexMatches
    {
        public bool IsMatchedButNoResults { get; }
        public int Count { get; }
        public IEnumerable<IMatch> Matches { get; }

        public RegexMatches( bool isMatchedButNoResults )
        {
            IsMatchedButNoResults = isMatchedButNoResults;

            Count = 0;
            Matches = [];
        }

        public RegexMatches( int count, IEnumerable<IMatch> matches )
        {
            Debug.Assert( matches != null );

            IsMatchedButNoResults = false;
            Count = count;
            Matches = matches;
        }

        public static RegexMatches Empty { get; } = new RegexMatches( 0, [] );
        public static RegexMatches MatchedButNoResults { get; } = new RegexMatches( isMatchedButNoResults: true );
    }
}
