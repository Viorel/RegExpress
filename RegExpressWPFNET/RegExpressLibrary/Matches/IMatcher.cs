using RegExpressLibrary.Matches;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches
{
	public interface IMatcher
	{
		RegexMatches Matches( string text, ICancellable cnc );
	}
}
