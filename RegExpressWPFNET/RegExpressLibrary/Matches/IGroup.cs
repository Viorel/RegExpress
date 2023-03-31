using System.Collections.Generic;
using RegExpressLibrary.Matches;


namespace RegExpressLibrary.Matches
{
	public interface IGroup : ICapture
	{
		bool Success { get; }

		string Name { get; }

		IEnumerable<ICapture> Captures { get; }
	}

}
