using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using RegExpressLibrary.Matches;


namespace RegExpressLibrary.Matches
{
	public interface IMatch : IGroup // TODO: reconsider the inheritance
	{
		IEnumerable<IGroup> Groups { get; }
	}
}
