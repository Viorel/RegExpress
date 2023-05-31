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
        readonly bool ProcessSurrogatePairs;
        readonly List<int>? SurrogatePairs;

        public SurrogatePairsHelper( string text, bool processSurrogatePairs )
        {
            ProcessSurrogatePairs = processSurrogatePairs;

            if( processSurrogatePairs )
            {
                SurrogatePairs = new List<int>( );
                CollectSurrogatePairs( text );
            }
            else
            {
                SurrogatePairs = null;
            }
        }


        public int ToTextIndex( int matchIndex )
        {
            if( !ProcessSurrogatePairs ) return matchIndex;

            int i = -1;
            while( ++i < SurrogatePairs!.Count )
            {
                if( SurrogatePairs[i] >= matchIndex ) break;
            }

            return matchIndex + i;
        }


        public (int textIndex, int textLength) ToTextIndexAndLength( int matchIndex, int matchLength )
        {
            if( !ProcessSurrogatePairs ) return (matchIndex, matchLength);

            var text_index = ToTextIndex( matchIndex );
            var text_length = ToTextIndex( matchIndex + matchLength ) - text_index;

            return (text_index, text_length);
        }


        public int ToMatchIndex( int textIndex )
        {
            if( !ProcessSurrogatePairs ) return textIndex;

            int n = 0;
            while( n < SurrogatePairs!.Count && SurrogatePairs[n] <= textIndex ) ++n;

            Debug.Assert( textIndex - n >= 0 );

            return textIndex - n;
        }


        void CollectSurrogatePairs( string text )
        {
            int mi = 0;
            for( int ti = 0; ti < text.Length; )
            {
                if( char.IsSurrogatePair( text, ti ) )
                {
                    SurrogatePairs!.Add( mi );
                    ti += 2;
                }
                else
                {
                    ++ti;
                }
                ++mi;
            }
        }
    }
}
