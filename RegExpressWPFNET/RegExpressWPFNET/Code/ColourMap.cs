using System.Collections.Generic;
using System.Diagnostics;
using RegExpressLibrary;


namespace RegExpressWPFNET.Code
{
    internal sealed class ColourMap
    {
        readonly int startOffset;
        readonly sbyte[] data; // 0 -- free, 1, 2, 3 -- colours, -1 -- intersection


        public ColourMap( int startOffset, int length )
        {
            this.startOffset = startOffset;
            this.data = new sbyte[length];
        }


        public void Set( int index, int length, sbyte colour )
        {
            Debug.Assert( length >= 0 );
            Debug.Assert( colour >= 1 );

            int i = int.Max( index - startOffset, 0 );
            int end = int.Min( i + length, data.Length );

            unchecked
            {
                for( ; i < end; ++i )
                {
                    switch( data[i] )
                    {
                    case 0: // free
                        data[i] = colour; // coloured
                        break;
                    case > 0: // coloured
                        data[i] = -1; // intersection
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

                    while( i < data.Length && data[i] != colour ) ++i;

                    if( i >= data.Length ) break;

                    int start = i;

                    if( cnc.IsCancellationRequested ) break;

                    while( i < data.Length && data[i] == colour ) ++i;

                    int length = i - start;

                    if( cnc.IsCancellationRequested ) break;

                    list.Add( new Segment( start + startOffset, length ) );
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

                    while( i < data.Length && data[i] != colour ) ++i;

                    if( i >= data.Length ) break;

                    int start = i;

                    if( cnc.IsCancellationRequested ) break;

                    while( i < data.Length && data[i] == colour ) ++i;

                    int length = i - start;

                    if( cnc.IsCancellationRequested ) break;

                    list.Add( (new Segment( start + startOffset, length ), styleInfo) );
                }
            }

            return list;
        }
    }
}
