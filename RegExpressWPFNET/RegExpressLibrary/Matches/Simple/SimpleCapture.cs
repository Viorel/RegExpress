using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
	public sealed class SimpleCapture : SimpleBase, ICapture
	{
		internal SimpleCapture( int index, int length, ISimpleTextGetter textGetter )
			: base( index, length, textGetter )
		{
		}


		internal SimpleCapture( int index, int length, int textIndex, int textLength, ISimpleTextGetter textGetter )
			: base( index, length, textIndex, textLength, textGetter )
		{
		}
	}
}
