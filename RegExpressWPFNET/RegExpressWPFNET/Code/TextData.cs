using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Controls;
using System.Windows.Documents;
using RegExpressWPFNET.Controls;

namespace RegExpressWPFNET.Code;

public class TextData
{
    int mLengthInTextElements = -1;
    int mNumberOfLines = -1;

    public string Text { get; } // (lines are separated by EOL specified in the call of 'GetTextData' which is also kept in 'Eol')
    public string Eol { get; }
    internal TextPointers TextPointers { get; } // (string index <--> 'TextPointer')

    internal TextData( string text, string eol, TextPointers pointers )
    {
        Debug.Assert( eol.Length == pointers.EolLength );

        Text = text;
        Eol = eol;
        TextPointers = pointers;
    }

    internal SelectionInfo Selection
    {
        get
        {
            MyRichTextBox rtb = (MyRichTextBox)TextPointers.Doc.Parent;
            SelectionInfo selection = new( 0, 0 );

            if( !rtb.Dispatcher.CheckAccess( ) )
            {
                rtb.Dispatcher.Invoke( ( ) =>
                {
                    selection = rtb.GetSelection( Eol );
                } );
            }
            else
            {
                selection = rtb.GetSelection( Eol );
            }

            return selection;
        }
    }

    public int LengthInTextElements
    {
        get
        {
            if( mLengthInTextElements < 0 )
            {
                lock( this )
                {
                    if( mLengthInTextElements < 0 )
                    {
                        // "\r\n" is counted as one element (in contrast to .NET Framework 4.8);
                        // workaround: replace '\r' with some character
                        StringInfo si = new( Text.Replace( '\r', 'x' ) );
                        // TODO: Reconsider in next versions of .NET

                        mLengthInTextElements = si.LengthInTextElements;
                    }
                }
            }

            return mLengthInTextElements;
        }
    }

    public int NumberOfLines
    {
        get
        {
            if( mNumberOfLines < 0 )
            {
                lock( this )
                {
                    if( mNumberOfLines < 0 )
                    {
                        if( string.IsNullOrEmpty( Text ) )
                        {
                            mNumberOfLines = 0;
                        }
                        else
                        {
                            Regex re = new( pattern: Regex.Escape( Eol ) );

                            mNumberOfLines = re.Matches( Text ).Count + 1;
                        }
                    }
                }
            }

            return mNumberOfLines;
        }
    }

    public TextData Export( string eol )
    {
        DbgValidateEol( eol );
        DbgValidateEol( Eol );

        string text;

        if( eol == Eol )
        {
            text = Text;
        }
        else
        {
            text = Text.Replace( Eol, eol );
        }

        TextPointers text_pointers;

        if( eol.Length == Eol.Length )
        {
            text_pointers = TextPointers;
        }
        else
        {
            text_pointers = new TextPointers( TextPointers.Doc, eol.Length );
        }

        TextData bd = new( text, eol, text_pointers );

        return bd;
    }



    [Conditional( "DEBUG" )]
    public static void DbgValidateEol( string eol )
    {
        Debug.Assert( eol == "\r\n" || eol == "\n\r" || eol == "\r" || eol == "\n" );
    }
}
