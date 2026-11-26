using System.Collections.Generic;
using System.Diagnostics;


namespace RegExpressLibrary
{
    public sealed class SurrogatePairsHelper
    {
        readonly List<int>? SurrogatePairs;

        public SurrogatePairsHelper( string text )
        {
            SurrogatePairs = new List<int>( );
            CollectSurrogatePairs( text );
        }

        public int ToCharIndex( int codepointIndex )
        {
            int i = -1;
            while( ++i < SurrogatePairs!.Count )
            {
                if( SurrogatePairs[i] >= codepointIndex ) break;
            }

            return codepointIndex + i;
        }

        public (int charIndex, int textLength) ToCharIndexAndLength( int codepointIndex, int codepointLength )
        {
            var char_index = ToCharIndex( codepointIndex );
            var char_length = ToCharIndex( codepointIndex + codepointLength ) - char_index;

            return (char_index, char_length);
        }

        public int ToCodepointIndex( int charIndex )
        {
            int n = 0;
            while( n < SurrogatePairs!.Count && SurrogatePairs[n] <= charIndex ) ++n;

            Debug.Assert( charIndex - n >= 0 );

            return charIndex - n;
        }

        void CollectSurrogatePairs( string text )
        {
            int mi = 0;
            for( int ti = 0; ti < text.Length; )
            {
                if( char.IsHighSurrogate( text, ti ) )
                {
                    SurrogatePairs!.Add( mi );
                    ti += 2;
                }
                else
                {
                    Debug.Assert( !char.IsLowSurrogate( text, ti ) );

                    ++ti;
                }
                ++mi;
            }
        }
    }
}
