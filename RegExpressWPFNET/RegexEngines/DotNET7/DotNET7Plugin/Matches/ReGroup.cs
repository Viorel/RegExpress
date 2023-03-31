using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using RegExpressLibrary.Matches;

namespace DotNET7Plugin.Matches
{
	class ReGroup : IGroup
	{
		readonly Group Group;

		public ReGroup( Group group )
		{
			Group = group;
		}


		#region ICapture

		public int Index => Group.Index;

		public int Length => Group.Length;

		public int TextIndex => Group.Index;

		public int TextLength => Group.Length;

		public string Value => Group.Value;

		#endregion ICapture


		#region IGroup

		public bool Success => Group.Success;

		public string Name => Group.Name;

		public IEnumerable<ICapture> Captures
		{
			get
			{
				return Group.Captures.OfType<Capture>( ).Select( c => new ReCapture( c ) );
			}
		}

		#endregion IGroup


		public override string ToString( ) => Group.ToString( );
	}
}
