using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace RegExpressWPFNET.Code
{
    internal sealed class ColourMap
    {
        readonly int mStartOffset;
        readonly sbyte[] mData; // 0 -- free, 1, 2, 3 -- colours, -1 -- intersection


        public ColourMap( int startOffset, int length )
        {
            mStartOffset = startOffset;
            mData = new sbyte[length];
        }


        public void Set( int index, int length, sbyte colour )
        {
            Debug.Assert( length >= 0 );
            Debug.Assert( colour >= 1 );

            int i = int.Max( index - mStartOffset, 0 );
            int end = int.Min( index - mStartOffset + length, mData.Length );

            unchecked
            {
                for( ; i < end; ++i )
                {
                    switch( mData[i] )
                    {
                    case 0: // free
                        mData[i] = colour; // coloured
                        break;
                    case > 0: // coloured
                        mData[i] = -1; // intersection
                        break;
                    }
                    // (intersections remain as is)
                }
            }
        }


        public List<Segment> GetSegments( ICancellable cnc, sbyte colour )
        {
            List<Segment> list = new( );

            unchecked
            {
                for( int i = 0; ; )
                {
                    if( cnc.IsCancellationRequested ) break;

                    while( i < mData.Length && mData[i] != colour ) ++i;

                    if( i >= mData.Length ) break;

                    int start = i;

                    if( cnc.IsCancellationRequested ) break;

                    while( i < mData.Length && mData[i] == colour ) ++i;

                    int length = i - start;

                    if( cnc.IsCancellationRequested ) break;

                    list.Add( new Segment( start + mStartOffset, length ) );
                }
            }

            return list;
        }


        public List<(Segment, StyleInfo)> GetSegments( ICancellable cnc, sbyte colour, StyleInfo styleInfo )
        {
            List<(Segment, StyleInfo)> list = new( );

            unchecked
            {
                for( int i = 0; ; )
                {
                    if( cnc.IsCancellationRequested ) break;

                    while( i < mData.Length && mData[i] != colour ) ++i;

                    if( i >= mData.Length ) break;

                    int start = i;

                    if( cnc.IsCancellationRequested ) break;

                    while( i < mData.Length && mData[i] == colour ) ++i;

                    int length = i - start;

                    list.Add( (new Segment( start + mStartOffset, length ), styleInfo) );
                }
            }

            return list;
        }
    }
}
