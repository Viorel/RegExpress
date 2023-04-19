using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    internal static class HighlightHelper
    {
        public enum ParKindEnum
        {
            Left,
            Right
        }

        public readonly struct Par
        {
            public readonly int Index;
            private readonly ParKindEnum Kind;

            public Par( int index, ParKindEnum kind )
            {
                Index = index;
                Kind = kind;
            }

            public bool IsLeft => Kind == ParKindEnum.Left;
            public bool IsRight => Kind == ParKindEnum.Right;
        }


        class ComparerByParIndex : IComparer<Par>
        {
            public int Compare( Par x, Par y )
            {
                return x.Index - y.Index;
            }

            public static readonly ComparerByParIndex Instance = new ComparerByParIndex( );
        }


        internal static void CommonHighlighting( ICancellable cnc, Highlights highlights, int selectionStart, int selectionEnd, Segment visibleSegment,
            List<Par> parentheses, int parSize, List<Par> brackets, int bracketSize )
        {
            parentheses.Sort( ComparerByParIndex.Instance ); //?
            brackets.Sort( ComparerByParIndex.Instance );

            int c = selectionStart;
            if( parentheses.Any( p => p.IsRight && p.Index == c - parSize ) ) c -= parSize;
            else if( parentheses.Any( p => p.IsLeft && p.Index == c ) ) c += parSize;

            ProcessParenthesesOrBrackets( cnc, highlights, c, visibleSegment, parSize, parentheses, isBracket: false );

            c = selectionStart;
            if( brackets.Any( p => p.IsRight && p.Index == c - bracketSize ) ) c -= bracketSize;
            else if( brackets.Any( p => p.IsLeft && p.Index == c ) ) c += bracketSize;

            ProcessParenthesesOrBrackets( cnc, highlights, c, visibleSegment, bracketSize, brackets, isBracket: true );
        }


        static void ProcessParenthesesOrBrackets( ICancellable cnc, Highlights highlights, int selectionStart, Segment visibleSegment, int size, List<Par> parentheses, bool isBracket )
        {
            // must be ordered by index
            Debug.Assert( !parentheses.Zip( parentheses.Skip( 1 ), ( a, b ) => a.Index < b.Index ).Any( c => !c ) );

            var parentheses_at_left = parentheses.Where( p => ( p.IsLeft && selectionStart > p.Index ) || ( p.IsRight && selectionStart > p.Index + ( size - 1 ) ) ).ToArray( );
            if( cnc.IsCancellationRequested ) return;

            var parentheses_at_right = parentheses.Where( p => ( p.IsLeft && selectionStart <= p.Index ) || ( p.IsRight && selectionStart <= p.Index + ( size - 1 ) ) ).ToArray( );
            if( cnc.IsCancellationRequested ) return;

            if( parentheses_at_left.Any( ) )
            {
                int n = 0;
                int found_i = -1;
                for( int i = parentheses_at_left.Length - 1; i >= 0; --i )
                {
                    if( cnc.IsCancellationRequested ) break;

                    var p = parentheses_at_left[i];
                    if( p.IsRight ) --n;
                    else if( p.IsLeft ) ++n;
                    if( n == +1 )
                    {
                        found_i = i;
                        break;
                    }
                }
                if( found_i >= 0 )
                {
                    var p = parentheses_at_left[found_i];
                    var s = new Segment( p.Index, size );

                    if( isBracket )
                        highlights.LeftBracket = s;
                    else
                        highlights.LeftPar = s;
                }
            }

            if( cnc.IsCancellationRequested ) return;

            if( parentheses_at_right.Any( ) )
            {
                int n = 0;
                int found_i = -1;
                for( int i = 0; i < parentheses_at_right.Length; ++i )
                {
                    if( cnc.IsCancellationRequested ) break;

                    var p = parentheses_at_right[i];
                    if( p.IsLeft ) --n;
                    else if( p.IsRight ) ++n;
                    if( n == +1 )
                    {
                        found_i = i;
                        break;
                    }
                }
                if( found_i >= 0 )
                {
                    var p = parentheses_at_right[found_i];
                    var s = new Segment( p.Index, size );

                    if( visibleSegment.Intersects( s ) )
                    {
                        if( isBracket )
                            highlights.RightBracket = s;
                        else
                            highlights.RightPar = s;
                    }
                }
            }
        }
    }
}
