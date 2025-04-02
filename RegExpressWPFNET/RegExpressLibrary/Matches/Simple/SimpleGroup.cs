using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
	public sealed class SimpleGroup : SimpleBase, IGroup
	{
		readonly List<ICapture> mCaptures = new( );


		internal SimpleGroup( int index, int length, ISimpleTextGetter textGetter,
			bool success, string name )
			: base( index, length, textGetter )
		{
			Success = success;
			Name = name;
		}


		internal SimpleGroup( int index, int length, int textIndex, int textLength, ISimpleTextGetter textGetter,
			bool success, string name )
			: base( index, length, textIndex, textLength, textGetter )
		{
			Success = success;
			Name = name;
		}


		#region IGroup

		public bool Success { get; }

		public string Name { get; private set; }

		public IEnumerable<ICapture> Captures => mCaptures;

		#endregion IGroup

		public SimpleCapture AddCapture( int index, int length )
		{
            TextGetter.Validate( index, length );

            var capture = new SimpleCapture( index, length, TextGetter );
			mCaptures.Add( capture );

			return capture;
		}


		public SimpleCapture AddCapture( int index, int length, int textIndex, int textLength )
		{
            TextGetter.Validate( index, length );

            var capture = new SimpleCapture( index, length, textIndex, textLength, TextGetter );
			mCaptures.Add( capture );

			return capture;
		}


		public void SetName( string name )
		{
			Name = name;
		}

	}
}
