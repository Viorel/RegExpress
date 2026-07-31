using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
    public sealed class SimpleGroup : SimpleBase, IGroup
    {
        readonly List<ICapture> mCaptures = [];


        internal SimpleGroup( int index, int length, int textIndex, int textLength, bool success, string name, ISimpleTextGetter textGetter )
            : base( index, length, textIndex, textLength, textGetter )
        {
            Success = success;
            Name = name;
        }


        #region IGroup

        public bool Success { get; }

        public string Name { get; private set; }

        public bool ValueOnly { get; private set; } = false;

        public IEnumerable<ICapture> Captures => mCaptures;

        #endregion IGroup

        public SimpleCapture AddCapture( int index, int length )
        {
            return AddCapture( index, length, index, length );
        }

        public SimpleCapture AddCapture( int nativeIndex, int nativeLength, int charIndex, int charLength )
        {
            TextGetter.ThrowIfInvalid( charIndex, charLength );

            var capture = new SimpleCapture( nativeIndex, nativeLength, charIndex, charLength, TextGetter );
            mCaptures.Add( capture );

            return capture;
        }

        public void SetName( string name )
        {
            Name = name;
        }

        public void SetValueOnly( )
        {
            ValueOnly = true;
        }
    }
}
