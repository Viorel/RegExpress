using System;
using System.Diagnostics;
using System.Linq;
using System.Windows.Documents;


namespace RegExpressWPFNET.Code
{
    public sealed class TextPointers
    {
        internal readonly FlowDocument Doc;
        internal readonly int EolLength;


        public TextPointers( FlowDocument doc, int eolLength )
        {
            Debug.Assert( doc != null );
            Debug.Assert( eolLength == 1 || eolLength == 2 );

            Doc = doc;
            EolLength = eolLength;
        }


        public TextPointer GetTextPointer( int index )
        {
            Debug.Assert( index >= 0 );

            int remaining_index = index;

            foreach( var block in Doc.Blocks )
            {
                var tb = FindTextPointerB( (dynamic)block, ref remaining_index );

                if( tb != null ) return tb;
            }

            return Doc.ContentEnd; //?
        }


        public ValueTuple<TextPointer, TextPointer> GetTextPointers( int index1, int index2 )
        {
            Debug.Assert( index1 >= 0 );
            Debug.Assert( index2 >= 0 );

            //return ValueTuple.Create( GetTextPointer( index1 ), GetTextPointer( index2 ) );	//

            RangeData rd = new RangeData( index1, index2 );

            foreach( var block in Doc.Blocks )
            {
                var r = FindTextPointersB( (dynamic)block, ref rd );
                if( r ) break;
            }

            var tp1 = rd.Pointer1 ?? Doc.ContentEnd;
            var tp2 = rd.Pointer2 ?? Doc.ContentEnd;

            return ValueTuple.Create( tp1, tp2 );
        }


        public int GetIndex( TextPointer tp, LogicalDirection dir )
        {
            Debug.Assert( tp.IsInSameDocument( Doc.ContentStart ) );

            tp = tp.GetInsertionPosition( dir );

            if( tp.Parent is FlowDocument ) return 0;

            TextElement parent = (TextElement)tp.Parent;

            int index = FindStartIndex( parent );
            if( index < 0 ) return -1;

            if( parent is Run )
            {
                return index + parent.ContentStart.GetOffsetToPosition( tp );
            }
            else
            {
                return index;
            }
        }


        //---


        TextPointer FindTextPointerB( Section section, ref int remainingIndex )
        {
            foreach( var block in section.Blocks )
            {
                var tp = FindTextPointerB( (dynamic)block, ref remainingIndex );

                if( tp != null ) return tp;
            }

            return null;
        }


        TextPointer FindTextPointerB( Paragraph paragraph, ref int remainingIndex )
        {
            if( remainingIndex <= 0 ) return paragraph.ContentStart.GetInsertionPosition( LogicalDirection.Forward );

            foreach( var inline in paragraph.Inlines )
            {
                var tp = FindTextPointerI( (dynamic)inline, ref remainingIndex );
                if( tp != null ) return tp;
            }

            if( remainingIndex <= 0 ) return paragraph.ContentEnd;

            remainingIndex -= EolLength;

            return null;
        }


        TextPointer FindTextPointerI( Span span, ref int remainingIndex )
        {
            foreach( var inline in span.Inlines )
            {
                var tp = FindTextPointerI( (dynamic)inline, ref remainingIndex );
                if( tp != null ) return tp;
            }

            return null;
        }


        TextPointer FindTextPointerI( Run run, ref int remainingIndex )
        {
            // Unfortunately, '\r[\n]' and '\n' are possible inside Run 
            //Debug.Assert( !run.Text.Contains( '\r' ) );
            //Debug.Assert( !run.Text.Contains( '\n' ) );

            char prev_c = '\0';
            int i = 0;

            foreach( char c in run.Text )
            {
                if( remainingIndex <= 0 ) return run.ContentStart.GetPositionAtOffset( i );

                switch( c )
                {
                case '\r':
                    remainingIndex -= EolLength;
                    break;
                case '\n':
                    if( prev_c == '\r' )
                    {
                        // ignore '\n' after '\r'
                    }
                    else
                    {
                        remainingIndex -= EolLength;
                    }
                    break;
                default:
                    --remainingIndex;
                    break;
                }

                prev_c = c;
                ++i;
            }

            return null;
        }


        TextPointer FindTextPointerI( LineBreak lb, ref int remainingIndex )
        {
            if( remainingIndex <= 0 ) return lb.ElementStart;

            remainingIndex -= EolLength;

            return null;
        }


        //---


        struct RangeData
        {
            public TextPointer Pointer1;
            public TextPointer Pointer2;
            public int Remaining1;
            public int Remaining2;

            public bool Done => Pointer1 != null && Pointer2 != null;

            public RangeData( int remaining1, int remaining2 ) : this( )
            {
                Remaining1 = remaining1;
                Remaining2 = remaining2;
            }
        }


        bool FindTextPointersB( Section section, ref RangeData rd )
        {
            foreach( var block in section.Blocks )
            {
                var r = FindTextPointersB( (dynamic)block, ref rd );
                if( r ) return true;
            }

            return false;
        }


        bool FindTextPointersB( Paragraph paragraph, ref RangeData rd )
        {
            if( rd.Pointer1 == null && rd.Remaining1 <= 0 ) rd.Pointer1 = paragraph.ContentStart;
            if( rd.Pointer2 == null && rd.Remaining2 <= 0 ) rd.Pointer2 = paragraph.ContentStart;

            if( rd.Done ) return true;

            foreach( var inline in paragraph.Inlines )
            {
                var r = FindTextPointersI( (dynamic)inline, ref rd );
                if( r ) return true;
            }

            if( rd.Pointer1 == null )
            {
                if( rd.Remaining1 <= 0 )
                    rd.Pointer1 = paragraph.ContentEnd;
                else
                    rd.Remaining1 -= EolLength;
            }

            if( rd.Pointer2 == null )
            {
                if( rd.Remaining2 <= 0 )
                    rd.Pointer2 = paragraph.ContentEnd;
                else
                    rd.Remaining2 -= EolLength;
            }

            return rd.Done;
        }


        bool FindTextPointersI( Span span, ref RangeData rd )
        {
            foreach( var inline in span.Inlines )
            {
                var r = FindTextPointersI( (dynamic)inline, ref rd );
                if( r ) return true;
            }

            return false;
        }


        bool FindTextPointersI( Run run, ref RangeData rd )
        {
            //Unfortunately, '\r[\n]' and '\n' are possible inside Run 
            //Debug.Assert( !run.Text.Contains( '\r' ) );
            //Debug.Assert( !run.Text.Contains( '\n' ) );

            char prev_c = '\0';
            int i = 0;

            foreach( char c in run.Text )
            {
                if( rd.Pointer1 == null && rd.Remaining1 <= 0 ) rd.Pointer1 = run.ContentStart.GetPositionAtOffset( i );
                if( rd.Pointer2 == null && rd.Remaining2 <= 0 ) rd.Pointer2 = run.ContentStart.GetPositionAtOffset( i );

                if( rd.Done ) return true;

                switch( c )
                {
                case '\r':
                    if( rd.Pointer1 == null ) rd.Remaining1 -= EolLength;
                    if( rd.Pointer2 == null ) rd.Remaining2 -= EolLength;
                    break;
                case '\n':
                    if( prev_c == '\r' )
                    {
                        // ignore '\n' after '\r'
                    }
                    else
                    {
                        if( rd.Pointer1 == null ) rd.Remaining1 -= EolLength;
                        if( rd.Pointer2 == null ) rd.Remaining2 -= EolLength;
                    }
                    break;
                default:
                    if( rd.Pointer1 == null ) --rd.Remaining1;
                    if( rd.Pointer2 == null ) --rd.Remaining2;
                    break;
                }

                prev_c = c;
                ++i;
            }

            return false;
        }


        bool FindTextPointersI( LineBreak lb, ref RangeData rd )
        {
            if( rd.Pointer1 == null )
            {
                if( rd.Remaining1 <= 0 )
                    rd.Pointer1 = lb.ContentStart;
                else
                    rd.Remaining1 -= EolLength;
            }

            if( rd.Pointer2 == null )
            {
                if( rd.Remaining2 <= 0 )
                    rd.Pointer2 = lb.ContentStart;
                else
                    rd.Remaining2 -= EolLength;
            }

            return rd.Done;
        }


        //---


        int FindStartIndex( TextElement el )
        {
            int index = 0;
            foreach( var block in Doc.Blocks )
            {
                if( FindStartIndexB( (dynamic)block, el, ref index ) ) return index;
            }

            return -1;
        }


        bool FindStartIndexB( Section section, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( section, el ) ) return true;

            foreach( var block in section.Blocks )
            {
                if( FindStartIndexB( (dynamic)block, el, ref index ) ) return true;
            }

            return false;
        }


        bool FindStartIndexB( Paragraph para, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( para, el ) ) return true;

            foreach( var inline in para.Inlines )
            {
                if( FindStartIndexI( (dynamic)inline, el, ref index ) ) return true;
            }

            index += EolLength;

            return false;
        }


        bool FindStartIndexI( Span span, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( span, el ) ) return true;

            foreach( var inline in span.Inlines )
            {
                if( FindStartIndexI( (dynamic)inline, el, ref index ) ) return true;
            }

            return false;
        }


        bool FindStartIndexI( Run run, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( run, el ) ) return true;

            char prev_c = '\0';

            foreach( char c in run.Text )
            {
                switch( c )
                {
                case '\r':
                    index += EolLength;
                    break;
                case '\n':
                    if( prev_c == '\r' )
                    {
                        // ignore '\n' after '\r'
                    }
                    else
                    {
                        index += EolLength;
                    }
                    break;
                default:
                    ++index;
                    break;
                }

                prev_c = c;
            }

            return false;
        }


        bool FindStartIndexI( LineBreak lb, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( lb, el ) ) return true;

            index += EolLength;

            return false;
        }

    }
}
