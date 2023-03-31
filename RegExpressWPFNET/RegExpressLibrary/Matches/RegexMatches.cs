using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace RegExpressLibrary.Matches
{
	public class RegexMatches
	{
		public int Count { get; }
		public IEnumerable<IMatch> Matches { get; }


		public RegexMatches( int count, IEnumerable<IMatch> matches )
		{
			Debug.Assert( matches != null );

			Count = count;
			Matches = matches;
		}


		public static RegexMatches Empty { get; } = new RegexMatches( 0, Enumerable.Empty<IMatch>( ) );
	}
}
