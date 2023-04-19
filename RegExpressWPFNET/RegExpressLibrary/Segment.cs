using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RegExpressLibrary
{
    public struct Segment
    {
        public int Index { get; }
        public int Length { get; }

        public Segment( int index, int length )
        {
            Debug.Assert( length >= 0 );

            Index = index;
            Length = length;
        }


        public Segment( Segment a )
            : this( a.Index, a.Length )
        {

        }


        public bool IsEmpty => Length == 0;
        public int End => Index + Length;

        public bool Contains( int x ) => !IsEmpty && x >= Index && x < End;


        public bool Intersects( Segment b )
        {
            var i = Math.Max( Index, b.Index );
            var e = Math.Min( End, b.End );

            return e > i;
        }


        //public bool Intersects( int bIndex, int bLength )
        //{
        //    var i = Math.Max( Index, bIndex );
        //    var e = Math.Min( End, bIndex + bLength );

        //    return e > i;
        //}


        public static readonly Segment Empty = new( 0, 0 );


        public static Segment Intersection( Segment a, Segment b )
        {
            return Intersection( a, b.Index, b.Length );
        }


        public static Segment Intersection( Segment a, int bIndex, int bLength )
        {
            var i = Math.Max( a.Index, bIndex );
            var e = Math.Min( a.End, bIndex + bLength );

            if( e < i ) return Empty;

            return new Segment( i, e - i );
        }


        public static void Except( List<Segment> list, Segment a )
        {
            Except( list, a.Index, a.Length );
        }


        public static void Except( List<Segment> list, int index, int length )
        {
            if( length == 0 ) return;

            int initial_count = list.Count;

            for( int i = 0; i < initial_count; ++i )
            {
                var s = list[i];
                if( s.IsEmpty ) continue;

                var s1 = LeftIntersection( s, index );
                var s2 = RightIntersection( s, index + length );

                if( s1.IsEmpty )
                {
                    if( s2.IsEmpty )
                    {
                        list[i] = Empty;
                    }
                    else
                    {
                        list[i] = s2;
                    }
                }
                else
                {
                    if( s2.IsEmpty )
                    {
                        list[i] = s1;
                    }
                    else
                    {
                        list[i] = s1;
                        list.Add( s2 );
                    }
                }
            }
        }


        static Segment LeftIntersection( Segment a, int bIndex )
        {
            var i = a.Index;
            var e = Math.Min( a.End, bIndex );

            if( e < i ) return Empty;

            return new Segment( i, e - i );
        }


        static Segment RightIntersection( Segment a, int bEnd )
        {
            var i = Math.Max( a.Index, bEnd );
            var e = a.End;

            if( e < i ) return Empty;

            return new Segment( i, e - i );
        }


        #region Object

        public override string ToString( )
        {
            return Length == 0 ? $"(empty at {Index})" : $"({Index}..{Index + Length - 1})";
        }


        public override bool Equals( object? obj )
        {
            if( !( obj is Segment ) ) return false;

            Segment a = (Segment)obj;

            return Index == a.Index && Length == a.Length;
        }


        public override int GetHashCode( )
        {
            return HashCode.Combine( Index, Length );
        }

        #endregion Object


        // Recommended overloads for structures

        public static bool operator ==( Segment left, Segment right )
        {
            return left.Equals( right );
        }


        public static bool operator !=( Segment left, Segment right )
        {
            return !( left == right );
        }

    }
}
