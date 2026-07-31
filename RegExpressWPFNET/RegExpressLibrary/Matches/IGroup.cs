using System.Collections.Generic;
using RegExpressLibrary.Matches;


namespace RegExpressLibrary.Matches
{
	public interface IGroup : ICapture
	{
		bool Success { get; }

		string Name { get; }

		bool ValueOnly { get; }

		IEnumerable<ICapture> Captures { get; }
	}

}
