using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches
{
	public interface ICapture
	{
		int Index { get; }

		int Length { get; }


		int TextIndex { get; }

		int TextLength { get; }


		string Value { get; }
	}

}
