using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


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

        public (int textIndex, int textLength) ToCharIndexAndLength( int codepointIndex, int codepointLength )
        {
            var text_index = ToCharIndex( codepointIndex );
            var text_length = ToCharIndex( codepointIndex + codepointLength ) - text_index;

            return (text_index, text_length);
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
