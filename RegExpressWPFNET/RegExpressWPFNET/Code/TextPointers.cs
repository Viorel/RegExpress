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
                var tb = FindTextPointer_Block( block, ref remaining_index );

                if( tb != null ) return tb;
            }

            return Doc.ContentEnd; //?
        }

        public (TextPointer start, TextPointer end) GetTextPointers( int index1, int index2 )
        {
            Debug.Assert( index1 >= 0 );
            Debug.Assert( index2 >= 0 );

            RangeData rd = new( index1, index2 );

            foreach( var block in Doc.Blocks )
            {
                var r = FindTextPointers_Block( block, ref rd );
                if( r ) break;
            }

            var tp1 = rd.Pointer1 ?? Doc.ContentEnd;
            var tp2 = rd.Pointer2 ?? Doc.ContentEnd;

            return (tp1, tp2);
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


        TextPointer? FindTextPointer_Block( Block block, ref int remainingIndex )
        {
            if( block is Paragraph para ) return FindTextPointer_Paragraph( para, ref remainingIndex );
            if( block is Section section ) return FindTextPointer_Section( section, ref remainingIndex );

            throw new NotSupportedException( );
        }

        TextPointer? FindTextPointer_Section( Section section, ref int remainingIndex )
        {
            foreach( var block in section.Blocks )
            {
                var tp = FindTextPointer_Block( block, ref remainingIndex );

                if( tp != null ) return tp;
            }

            return null;
        }

        TextPointer? FindTextPointer_Paragraph( Paragraph paragraph, ref int remainingIndex )
        {
            if( remainingIndex <= 0 ) return paragraph.ContentStart.GetInsertionPosition( LogicalDirection.Forward );

            foreach( var inline in paragraph.Inlines )
            {
                var tp = FindTextPointer_Inline( inline, ref remainingIndex );
                if( tp != null ) return tp;
            }

            if( remainingIndex <= 0 ) return paragraph.ContentEnd;

            remainingIndex -= EolLength;

            return null;
        }

        TextPointer? FindTextPointer_Inline( Inline inline, ref int remainingIndex )
        {
            if( inline is Run run ) return FindTextPointer_Run( run, ref remainingIndex );
            if( inline is Span span ) return FindTextPointer_Span( span, ref remainingIndex );
            if( inline is LineBreak lb ) return FindTextPointer_LineBreak( lb, ref remainingIndex );

            throw new NotSupportedException( );
        }

        TextPointer? FindTextPointer_Span( Span span, ref int remainingIndex )
        {
            foreach( var inline in span.Inlines )
            {
                var tp = FindTextPointer_Inline( inline, ref remainingIndex );
                if( tp != null ) return tp;
            }

            return null;
        }

        TextPointer? FindTextPointer_Run( Run run, ref int remainingIndex )
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

        TextPointer? FindTextPointer_LineBreak( LineBreak lb, ref int remainingIndex )
        {
            if( remainingIndex <= 0 ) return lb.ElementStart;

            remainingIndex -= EolLength;

            return null;
        }


        //---


        struct RangeData
        {
            public TextPointer? Pointer1;
            public TextPointer? Pointer2;
            public int Remaining1;
            public int Remaining2;

            public bool Done => Pointer1 != null && Pointer2 != null;

            public RangeData( int remaining1, int remaining2 ) : this( )
            {
                Remaining1 = remaining1;
                Remaining2 = remaining2;
            }
        }

        bool FindTextPointers_Block( Block block, ref RangeData rd )
        {
            if( block is Paragraph para ) return FindTextPointers_Paragraph( para, ref rd );
            if( block is Section section ) return FindTextPointers_Section( section, ref rd );

            throw new NotSupportedException( );
        }

        bool FindTextPointers_Section( Section section, ref RangeData rd )
        {
            foreach( var block in section.Blocks )
            {
                var r = FindTextPointers_Block( block, ref rd );
                if( r ) return true;
            }

            return false;
        }

        bool FindTextPointers_Paragraph( Paragraph paragraph, ref RangeData rd )
        {
            if( rd.Pointer1 == null && rd.Remaining1 <= 0 ) rd.Pointer1 = paragraph.ContentStart;
            if( rd.Pointer2 == null && rd.Remaining2 <= 0 ) rd.Pointer2 = paragraph.ContentStart;

            if( rd.Done ) return true;

            foreach( var inline in paragraph.Inlines )
            {
                var r = FindTextPointers_Inline( inline, ref rd );
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

        bool FindTextPointers_Inline( Inline inline, ref RangeData rd )
        {
            if( inline is Run run ) return FindTextPointers_Run( run, ref rd );
            if( inline is Span span ) return FindTextPointers_Span( span, ref rd );
            if( inline is LineBreak lb ) return FindTextPointers_LineBreak( lb, ref rd );

            throw new NotSupportedException( );
        }

        bool FindTextPointers_Span( Span span, ref RangeData rd )
        {
            foreach( var inline in span.Inlines )
            {
                var r = FindTextPointers_Inline( inline, ref rd );
                if( r ) return true;
            }

            return false;
        }

        bool FindTextPointers_Run( Run run, ref RangeData rd )
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

        bool FindTextPointers_LineBreak( LineBreak lb, ref RangeData rd )
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
                if( FindStartIndex_Block( block, el, ref index ) ) return index;
            }

            return -1;
        }

        bool FindStartIndex_Block( Block block, TextElement el, ref int index )
        {
            if( block is Paragraph para ) return FindStartIndex_Paragraph( para, el, ref index );
            if( block is Section section ) return FindStartIndex_Section( section, el, ref index );

            throw new NotSupportedException( );
        }


        bool FindStartIndex_Section( Section section, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( section, el ) ) return true;

            foreach( var block in section.Blocks )
            {
                if( FindStartIndex_Block( block, el, ref index ) ) return true;
            }

            return false;
        }


        bool FindStartIndex_Paragraph( Paragraph para, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( para, el ) ) return true;

            foreach( var inline in para.Inlines )
            {
                if( FindStartIndex_Inline( inline, el, ref index ) ) return true;
            }

            index += EolLength;

            return false;
        }

        bool FindStartIndex_Inline( Inline inline, TextElement el, ref int index )
        {
            if( inline is Run run ) return FindStartIndex_Run( run, el, ref index );
            if( inline is Span span ) return FindStartIndex_Span( span, el, ref index );
            if( inline is LineBreak lb ) return FindStartIndex_LineBreak( lb, el, ref index );

            throw new NotSupportedException( );
        }

        bool FindStartIndex_Span( Span span, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( span, el ) ) return true;

            foreach( var inline in span.Inlines )
            {
                if( FindStartIndex_Inline( inline, el, ref index ) ) return true;
            }

            return false;
        }

        bool FindStartIndex_Run( Run run, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( run, el ) ) return true;

            string text = run.Text;
            int start = 0;

            for(; ; )
            {
                int i = text.IndexOf( '\r', start );

                if( i < 0 )
                {
                    index += text.Length - start;

                    break;
                }

                index += i - start;
                index += EolLength;

                start = i + 1;

                if( start < text.Length && text[start] == '\n' )
                {
                    // ignore '\n' after '\r'

                    ++start;
                }
            }

            return false;
        }

        bool FindStartIndex_LineBreak( LineBreak lb, TextElement el, ref int index )
        {
            if( object.ReferenceEquals( lb, el ) ) return true;

            index += EolLength;

            return false;
        }
    }
}
