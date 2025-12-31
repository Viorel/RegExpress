using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Unicode;
using System.Threading.Tasks;
using System.Windows.Documents;


namespace RegExpressWPFNET.Code
{
    /*
    // 21A9 ↩
    // 21B2 ↲
    // 21B5 ↵
    // 23CE ⏎

    // 00B7 ·
    // 2219 ∙
    // 22C5 ⋅
    // 23B5 ⎵
    // 2E31 ⸱ 
    // 2420 ␠
    // 2423 ␣

    // 2192 →
    // 21E2 ⇢
    // 21E5 ⇥
    // 2589 ▷
    // 25B6 ▶
    // 25B8 ▸ 
    // 25B9 ▹ 
    // 2B62 ⭢
    // 2B72 ⭲
    // 2B6C ⭬
     */


    sealed class OutputBuilder
    {
        struct MyRun
        {
            public string text;
            public bool isSpecial;
        }

        readonly StringBuilder sb = new( );
        readonly List<MyRun> runs = new( );
        readonly StyleInfo? specialStyleInfo;
        bool isPreviousSpecial = false;


        public OutputBuilder( StyleInfo? specialStyleInfo )
        {
            this.specialStyleInfo = specialStyleInfo;
        }


        public (Inline inline, string plainText) Build( string text, TextPointer at, int maxLength = int.MaxValue )
        {
            sb.Clear( );
            runs.Clear( );
            isPreviousSpecial = false;
            int realLength = 0;

            for( int i = 0; i < text.Length; i++ )
            {
                if( realLength >= maxLength )
                {
                    realLength += AppendSpecial( @"…" );

                    break;
                }

                char c = text[i];

                switch( c )
                {
                case '\r':
                    realLength += AppendSpecial( @"\r" );
                    continue;
                case '\n':
                    realLength += AppendSpecial( @"\n" );
                    continue;
                case '\t':
                    realLength += AppendSpecial( @"\t" );
                    continue;
                }

                if( ( c >= 0x21 && c <= 0x7E ) ||
                    c == ' ' ||
                    c == '$' ||
                    c == '€' ||
                    c == '£' ||
                    c == '₣' ||
                    c == '¢' ||
                    c == '¥' ||
                    c == '¤' ||
                    ( c >= UnicodeRanges.Hebrew.FirstCodePoint && c < UnicodeRanges.Hebrew.FirstCodePoint + UnicodeRanges.Hebrew.Length && char.GetUnicodeCategory( c ) == UnicodeCategory.OtherLetter )
                  )
                {
                    realLength += AppendNormal( c );
                    continue;
                }

                switch( char.GetUnicodeCategory( c ) )
                {
                case UnicodeCategory.UppercaseLetter:
                case UnicodeCategory.LowercaseLetter:
                    /*
                case UnicodeCategory.TitlecaseLetter:
                //case UnicodeCategory.ModifierLetter:
                case UnicodeCategory.OtherLetter:
                //case UnicodeCategory.NonSpacingMark:
                //case UnicodeCategory.SpacingCombiningMark:
                //case UnicodeCategory.EnclosingMark:
                case UnicodeCategory.DecimalDigitNumber:
                case UnicodeCategory.LetterNumber:
                case UnicodeCategory.OtherNumber:
                case UnicodeCategory.SpaceSeparator:
                //case UnicodeCategory.LineSeparator:
                //case UnicodeCategory.ParagraphSeparator:
                //case UnicodeCategory.Control:
                //case UnicodeCategory.Format:
                //case UnicodeCategory.Surrogate:
                //case UnicodeCategory.PrivateUse:
                case UnicodeCategory.ConnectorPunctuation:
                case UnicodeCategory.DashPunctuation:
                case UnicodeCategory.OpenPunctuation:
                case UnicodeCategory.ClosePunctuation:
                case UnicodeCategory.InitialQuotePunctuation:
                case UnicodeCategory.FinalQuotePunctuation:
                case UnicodeCategory.OtherPunctuation:
                case UnicodeCategory.MathSymbol:
                case UnicodeCategory.CurrencySymbol:
                //case UnicodeCategory.ModifierSymbol:
                case UnicodeCategory.OtherSymbol:
                //case UnicodeCategory.OtherNotAssigned:
                      */
                    realLength += AppendNormal( c );

                    break;

                default:

                    realLength += AppendCode( c );

                    break;
                }
            }

            // last
            if( sb.Length > 0 )
            {
                runs.Add( new MyRun { text = sb.ToString( ), isSpecial = isPreviousSpecial } );
            }

            // TODO: maybe insert element at position after creation.

            switch( runs.Count )
            {
            case 0:
                return (new Span( (Inline)null!, at ), "");
            case 1:
            {
                var r = runs[0];
                var run = new Run( r.text, at );
                if( r.isSpecial )
                {
                    Debug.Assert( specialStyleInfo != null );

                    run.Style( specialStyleInfo );
                }

                return (run, r.text);
            }
            default:
            {
                string plain_text = "";
                var r = runs[0];
                plain_text += r.text;
                var run = new Run( r.text );
                if( r.isSpecial )
                {
                    Debug.Assert( specialStyleInfo != null );

                    run.Style( specialStyleInfo );
                }

                Span span = new( run, at );

                for( int i = 1; i < runs.Count; ++i )
                {
                    r = runs[i];
                    plain_text += r.text;
                    run = new Run( r.text, span.ContentEnd );
                    if( r.isSpecial )
                    {
                        Debug.Assert( specialStyleInfo != null );

                        run.Style( specialStyleInfo );
                    }
                }

                return (span, plain_text);
            }
            }
        }


        private int AppendSpecial( string s )
        {
            if( isPreviousSpecial || specialStyleInfo == null )
            {
                sb.Append( s );
            }
            else
            {
                Debug.Assert( specialStyleInfo != null );

                if( sb.Length > 0 )
                {
                    runs.Add( new MyRun { text = sb.ToString( ), isSpecial = false } );
                }
                sb.Clear( ).Append( s );
                isPreviousSpecial = true;
            }
            return s.Length;
        }


        private int AppendCode( char c )
        {
            return AppendSpecial( $@"\u{(int)c:X4}" );
        }


        private int AppendNormal( char c )
        {
            if( !isPreviousSpecial )
            {
                sb.Append( c );
            }
            else
            {
                Debug.Assert( specialStyleInfo != null );

                if( sb.Length > 0 )
                {
                    runs.Add( new MyRun { text = sb.ToString( ), isSpecial = true } );
                }
                sb.Clear( ).Append( c );
                isPreviousSpecial = false;
            }
            return 1;
        }
    }
}
