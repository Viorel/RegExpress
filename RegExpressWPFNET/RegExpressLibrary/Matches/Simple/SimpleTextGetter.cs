using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
	public class SimpleTextGetter : ISimpleTextGetter
	{
		readonly string Text;

		public SimpleTextGetter( string text )
		{
			Text = text;
		}


		#region ISimpleTextGetter

		public string GetText( int index, int length )
		{
			return Text.Substring( index, length );
		}

		#endregion ISimpleTextGetter

	}
}
