using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary.Matches.Simple
{
    public sealed class SimpleMatch : SimpleBase, IMatch
    {
        readonly List<IGroup> mGroups = [];

        private SimpleMatch( int nativeIndex, int nativeLength, int charIndex, int charLength, ISimpleTextGetter textGetter )
            : base( nativeIndex, nativeLength, charIndex, charLength, textGetter )
        {
        }

        public static SimpleMatch Create( int index, int length, ISimpleTextGetter textGetter )
        {
            return Create( index, length, index, length, textGetter );
        }

        public static SimpleMatch Create( int nativeIndex, int nativeLength, int charIndex, int charLength, ISimpleTextGetter textGetter )
        {
            textGetter.ThrowIfInvalid( charIndex, charLength );

            return new SimpleMatch( nativeIndex, nativeLength, charIndex, charLength, textGetter );
        }


        #region IMatch

        public IEnumerable<IGroup> Groups => mGroups;

        public bool Success { get; } = true; // TODO: reconsider the inheritance

        public string Name { get; } = ""; // TODO: reconsider the inheritance

        #endregion IMatch


        #region IGroup

        public IEnumerable<ICapture> Captures
        {
            get
            {
                // Not expected to be called.

                throw new InvalidOperationException( );
            }
        }

        public bool ValueOnly
        {
            get
            {
                // Not expected to be called.

                throw new InvalidOperationException( );
            }
        }

        #endregion IGroup

        public SimpleGroup AddDefaultGroup( )
        {
            return AddSucceededGroup( NativeIndex, NativeLength, CharIndex, CharLength, "" );
        }

        public SimpleGroup AddFailedGroup( string name )
        {
            SimpleGroup group = new( 0, 0, 0, 0, false, name, TextGetter );
            mGroups.Add( group );

            return group;
        }

        public SimpleGroup AddSucceededGroup( int index, int length, string name )
        {
            return AddSucceededGroup( index, length, index, length, name );
        }

        public SimpleGroup AddSucceededGroup( int nativeIndex, int nativeLength, int charIndex, int charLength, string name )
        {
            TextGetter.ThrowIfInvalid( charIndex, charLength );

            SimpleGroup group = new( nativeIndex, nativeLength, charIndex, charLength, true, name, TextGetter );
            mGroups.Add( group );

            return group; 
        }

        public SimpleGroup AddSucceededNoDetailsGroup( string name, string value )
        {
            SimpleGroup group = new( NativeIndex, NativeLength, CharIndex, value.Length, true, name, new TrivialTextGetter( value ) );
            group.SetValueOnly( );
            mGroups.Add( group );


            return group;
        }


        public void SetGroupName( int index, string name )
        {
            ( (SimpleGroup)mGroups[index] ).SetName( name );
        }
    }
}
