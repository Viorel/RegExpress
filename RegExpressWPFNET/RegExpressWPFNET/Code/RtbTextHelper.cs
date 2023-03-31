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
        readonly StringBuilder Sb = new StringBuilder( );
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
                ProcessBlock( (dynamic)block );
            }

            return Sb.ToString( );
        }


        void ProcessBlock( Section section )
        {
            foreach( Block block in section.Blocks )
            {
                ProcessBlock( (dynamic)block );
            }
        }


        void ProcessBlock( Paragraph para )
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
                ProcessInline( (dynamic)inline );
            }
        }


        void ProcessInline( Span span )
        {
            foreach( Inline inline in span.Inlines )
            {
                ProcessInline( (dynamic)inline );
            }
        }


        void ProcessInline( Run run )
        {
            //Unfortunately, '\r[\n]' and '\n' are possible inside Run 
            //Debug.Assert( !run.Text.Contains( '\r' ) );
            //Debug.Assert( !run.Text.Contains( '\n' ) );
            //Sb.Append( run.Text );

            char prev_c = '\0';

            foreach( char c in run.Text )
            {
                switch( c )
                {
                case '\r':
                    Sb.Append( Eol );
                    break;
                case '\n':
                    if( prev_c == '\r' )
                    {
                        // ignore '\n' after '\r'
                    }
                    else
                    {
                        Sb.Append( Eol );
                    }
                    break;
                default:
                    Sb.Append( c );
                    break;
                }

                prev_c = c;
            }

        }


        void ProcessInline( LineBreak lb )
        {
            Sb.Append( Eol );
        }
    }
}
