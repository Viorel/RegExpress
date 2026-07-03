using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
	public sealed class SimpleCapture : SimpleBase, ICapture
	{
		internal SimpleCapture( int nativeIindex, int nativeLength, int charIndex, int charLength, ISimpleTextGetter textGetter )
			: base( nativeIindex, nativeLength, charIndex, charLength, textGetter )
		{
		}
	}
}
