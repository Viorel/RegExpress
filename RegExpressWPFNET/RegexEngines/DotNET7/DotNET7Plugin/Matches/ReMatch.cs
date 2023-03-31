using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using RegExpressLibrary.Matches;

namespace DotNET7Plugin.Matches
{
	class ReMatch : IMatch
	{
		readonly Match Match;

		public ReMatch( Match match )
		{
			Match = match;
		}


		#region ICapture

		public int Index => Match.Index;

		public int Length => Match.Length;

		public int TextIndex => Match.Index;

		public int TextLength => Match.Length;

		public string Value => Match.Value;

		#endregion ICapture


		#region IGroup

		public bool Success => Match.Success;

		public string Name => Match.Name;

		public IEnumerable<ICapture> Captures
		{
			get
			{
				return Match.Captures.OfType<Capture>( ).Select( c => new ReCapture( c ) );
			}
		}

		#endregion IGroup


		#region IMatch

		public IEnumerable<IGroup> Groups
		{
			get
			{
				return Match.Groups.OfType<Group>( ).Select( g => new ReGroup( g ) );
			}
		}

		#endregion IMatch


		public override string ToString( ) => Match.ToString( );
	}
}
