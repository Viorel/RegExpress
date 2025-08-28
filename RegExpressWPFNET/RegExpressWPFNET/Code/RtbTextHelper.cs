using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Documents;


namespace RegExpressWPFNET.Code
{
    internal sealed class RtbTextHelper
    {
        readonly FlowDocument Doc;
        readonly string Eol;
        readonly StringBuilder Sb = new( );
        bool IsAnotherParagraph = false;


        internal RtbTextHelper( FlowDocument doc, string eol )
        {
            Debug.Assert( doc != null );
            Debug.Assert( eol == "\r" || eol == "\n" || eol == "\r\n" );

            Doc = doc;
            Eol = eol;
        }

        internal string GetText( )
        {
            Sb.Clear( );
            IsAnotherParagraph = false;

            foreach( Block block in Doc.Blocks )
            {
                ProcessBlock( block );
            }

            return Sb.ToString( );
        }

        void ProcessBlock( Block block )
        {
            if( block is Paragraph para ) { ProcessParagraph( para ); return; }
            if( block is Section section ) { ProcessSection( section ); return; }

            throw new NotSupportedException( );
        }

        void ProcessSection( Section section )
        {
            foreach( Block block in section.Blocks )
            {
                ProcessBlock( block );
            }
        }

        void ProcessParagraph( Paragraph para )
        {
            if( IsAnotherParagraph )
            {
                Sb.Append( Eol );
            }
            else
            {
                IsAnotherParagraph = true;
            }

            foreach( Inline inline in para.Inlines )
            {
                ProcessInline( inline );
            }
        }

        void ProcessInline( Inline inline )
        {
            if( inline is Run run ) { ProcessRun( run ); return; }
            if( inline is Span span ) { ProcessSpan( span ); return; }
            if( inline is LineBreak lb ) { ProcessLineBreak( lb ); return; }

            throw new NotSupportedException( );
        }

        void ProcessSpan( Span span )
        {
            foreach( Inline inline in span.Inlines )
            {
                ProcessInline( inline );
            }
        }

        void ProcessRun( Run run )
        {
            //Unfortunately, '\r[\n]' and '\n' are possible inside Run 
            //Debug.Assert( !run.Text.Contains( '\r' ) );
            //Debug.Assert( !run.Text.Contains( '\n' ) );
            //Sb.Append( run.Text );

            string text = run.Text;
            int start = 0;

            for(; ; )
            {
                int i = text.IndexOf( '\r', start );

                if( i < 0 )
                {
                    Sb.Append( text, start, text.Length - start );

                    break;
                }

                Sb.Append( text, start, i - start );
                Sb.Append( Eol );

                start = i + 1;

                if( start < text.Length && text[start] == '\n' )
                {
                    // ignore '\n' after '\r'

                    ++start;
                }
            }
        }

        void ProcessLineBreak( LineBreak _ )
        {
            Sb.Append( Eol );
        }
    }
}
