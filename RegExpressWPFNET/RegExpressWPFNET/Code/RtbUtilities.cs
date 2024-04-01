using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Threading;
using RegExpressLibrary;


namespace RegExpressWPFNET.Code
{

    public class BaseTextData
    {
        private int mLengthInTextElements = -1;

        public readonly string Text; // (lines are separated by EOL specified in the call of 'GetBaseTextData' and 'GetTextData',
        public readonly string Eol;  //  which is also kept in 'Eol')
        internal readonly TextPointers TextPointers; // (maps string index of 'Text' to 'TextPointer')

        internal BaseTextData( string text, string eol, TextPointers pointers )
        {
            Debug.Assert( eol.Length == pointers.EolLength );

            Text = text;
            Eol = eol;
            TextPointers = pointers;
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
                            //var si = new StringInfo( Text );

                            // For some reasons, "\r\n" is counted as one element (in contrast to .NET Framework 4.8)
                            // Workaround:
                            var si = new StringInfo( Text.Replace( "\r", "x" ) );
                            // TODO: Reconsider in next versions of .NET

                            mLengthInTextElements = si.LengthInTextElements;
                        }
                    }
                }

                return mLengthInTextElements;
            }
        }
    }


    public sealed class TextData : BaseTextData
    {
        public readonly int SelectionStart;
        public readonly int SelectionEnd;


        internal TextData( string text, string eol, TextPointers pointers, int selectionStart, int selectionEnd )
            : base( text, eol, pointers )
        {
            SelectionStart = selectionStart;
            SelectionEnd = selectionEnd;
        }
    }


    public static partial class RtbUtilities
    {
        const int MAX_BLOCKING_TIME_MS = 222;
        const int MAX_SEGMENT_LENGTH = 100;


        public static void SetText( RichTextBox rtb, string? text )
        {
            using( rtb.DeclareChangeBlock( ) )
            {
                rtb.Document.Blocks.Clear( );

                foreach( var s in SplitLinesRegex( ).Split( text ?? "" ) )
                {
                    rtb.Document.Blocks.Add( new Paragraph( new Run( s ) ) );
                }
            }
        }


        public static BaseTextData GetBaseTextDataInternal( RichTextBox rtb, string eol )
        {
            DbgValidateEol( eol );

            FlowDocument doc = rtb.Document;
            RtbTextHelper th = new RtbTextHelper( doc, eol );

            string text = th.GetText( );

            return new BaseTextData( text, eol, new TextPointers( doc, eol.Length ) );
        }


        public static BaseTextData GetBaseTextDataFrom( RichTextBox rtb, BaseTextData btd, string eol )
        {
            DbgValidateEol( eol );
            DbgValidateEol( btd.Eol );
            Debug.Assert( object.ReferenceEquals( rtb.Document, btd.TextPointers.Doc ) );

            string text;
            TextPointers textpointers;

            if( btd.Eol == eol )
            {
                text = btd.Text;
            }
            else
            {
                text = btd.Text.Replace( btd.Eol, eol );
            }

            if( btd.Eol.Length == eol.Length )
            {
                textpointers = btd.TextPointers;
            }
            else
            {
                textpointers = new TextPointers( rtb.Document, eol.Length );

            }

            return new BaseTextData( text, eol, textpointers );
        }


        public static TextData GetTextDataFrom( RichTextBox rtb, BaseTextData btd, string eol )
        {
            DbgValidateEol( eol );
            DbgValidateEol( btd.Eol );
            Debug.Assert( object.ReferenceEquals( rtb.Document, btd.TextPointers.Doc ) );

            string text;
            TextPointers textpointers;

            if( btd.Eol == eol )
            {
                text = btd.Text;
            }
            else
            {
                text = btd.Text.Replace( btd.Eol, eol );
            }

            if( btd.Eol.Length == eol.Length )
            {
                textpointers = btd.TextPointers;
            }
            else
            {
                textpointers = new TextPointers( rtb.Document, eol.Length );
            }

            var (selection_start, selection_end) = GetSelection( rtb.Selection, textpointers );

            return new TextData( text, eol, textpointers, selection_start, selection_end );
        }


        static (int selection_start, int selection_end) GetSelection( TextSelection selection, TextPointers pointers )
        {
            // TODO: implement 'pointers.GetIndices' that takes two text pointers
            int selection_start = Math.Max( 0, pointers.GetIndex( selection.Start, LogicalDirection.Backward ) );
            int selection_end = Math.Max( 0, pointers.GetIndex( selection.End, LogicalDirection.Forward ) );

            return (selection_start, selection_end);
        }


        public static void SafeSelect( RichTextBox rtb, TextData td, int selectionStart, int selectionEnd )
        {
            var tps = td.TextPointers.GetTextPointers( selectionStart, selectionEnd );

            rtb.Selection.Select( tps.Item1, tps.Item2 );
        }


        public static TextRange Range( this BaseTextData td, int start, int len )
        {
            var tps = td.TextPointers.GetTextPointers( start, start + len );
            var range = new TextRange( tps.Item1, tps.Item2 );

            return range;
        }


        public static TextRange Range0F( this BaseTextData td, int start, int len )
        {
            var tps = td.TextPointers.GetTextPointers( start, start + len );
            var range = new TextRange( tps.Item1, tps.Item2.GetInsertionPosition( LogicalDirection.Forward ) );

            return range;
        }


        public static TextRange Range0B( this BaseTextData td, int start, int len )
        {
            var tps = td.TextPointers.GetTextPointers( start, start + len );
            var range = new TextRange( tps.Item1, tps.Item2.GetInsertionPosition( LogicalDirection.Backward ) );

            return range;
        }


        public static TextRange RangeFB( this BaseTextData td, int start, int len )
        {
            var tps = td.TextPointers.GetTextPointers( start, start + len );
            var range = new TextRange( tps.Item1.GetInsertionPosition( LogicalDirection.Forward ), tps.Item2.GetInsertionPosition( LogicalDirection.Backward ) );

            return range;
        }


        public static TextRange Range( this TextData td, Segment segment )
        {
            return Range( td, segment.Index, segment.Length );
        }


        //


        public static TextRange Style( this TextRange range, StyleInfo styleInfo )
        {
            foreach( var style_info in styleInfo.Values )
            {
                range.ApplyPropertyValue( style_info.prop, style_info.val );
            }

            return range;
        }


        public static TextRange Style( this TextRange range, params StyleInfo[] styleInfos )
        {
            foreach( var styleInfo in styleInfos )
            {
                Style( range, styleInfo );
            }

            return range;
        }


        public static Inline Style( this Inline inline, StyleInfo styleInfo )
        {
            foreach( var style_info in styleInfo.Values )
            {
                inline.SetValue( style_info.prop, style_info.val );
            }

            return inline;
        }


        public static Inline Style( this Inline inline, params StyleInfo[] styleInfos )
        {
            foreach( var style_info in styleInfos )
            {
                Style( inline, style_info );
            }

            return inline;
        }


        public static bool ApplyStyle( ICancellable reh, ChangeEventHelper ceh, ProgressBar? pb, TextData td, IReadOnlyList<(Segment segment, StyleInfo styleInfo)> segmentsAndStyles )
        {
            // split into smaller segments

            var segments = new List<(int index, int length, StyleInfo styleInfo)>( segmentsAndStyles.Count );

            foreach( var segment_and_style in segmentsAndStyles )
            {
                int j = segment_and_style.segment.Index;
                int rem = segment_and_style.segment.Length;

                do
                {
                    if( reh.IsCancellationRequested ) return false;

                    int len = Math.Min( MAX_SEGMENT_LENGTH, rem );

                    segments.Add( (j, len, segment_and_style.styleInfo) );

                    j += len;
                    rem -= len;

                } while( rem > 0 );
            }


            int show_pb_time = unchecked(Environment.TickCount + 333); // (ignore overflow)
            int last_i = segments.Count;

            if( pb != null )
            {
                ceh.Invoke( CancellationToken.None, ( ) => //...
                {
                    pb.Visibility = Visibility.Hidden;
                    pb.Maximum = last_i;
                } );
            }

            //var rnd = new Random( );
            //segments = segments.OrderBy( s => rnd.Next() ).ToList( ); // just for fun

            //...
            //Debug.WriteLine( $"Total segments: {segments.Count}" );

            for( int i = 0; i < last_i; )
            {
                if( reh.IsCancellationRequested ) return false;

                ceh.Invoke( CancellationToken.None, ( ) =>
                {
                    if( pb != null )
                    {
                        if( Environment.TickCount > show_pb_time )
                        {
                            pb.Value = i;
                            pb.Visibility = Visibility.Visible;
                        }
                    }

                    var end = Environment.TickCount + MAX_BLOCKING_TIME_MS;
                    //int dbg_i = i;//...
                    do
                    {
                        //if( reh.IsAnyRequested ) return false;

                        var segment = segments[i];
                        td.Range0F( segment.index, segment.length ).Style( segment.styleInfo );

                    } while( ++i < last_i && Environment.TickCount < end );

                    //Debug.WriteLine( $"Subsegments: {i - dbg_i}" ); //...

                } );
            }

            return true;
        }


        public static bool ApplyStyle( ICancellable reh, ChangeEventHelper ceh, ProgressBar? pb, TextData td, IList<Segment> segments0, StyleInfo styleInfo )
        {
            // split into smaller segments

            var segments = new List<Segment>( segments0.Count );

            foreach( var segment in segments0 )
            {
                int j = segment.Index;
                int rem = segment.Length;

                do
                {
                    if( reh.IsCancellationRequested ) return false;

                    int len = Math.Min( MAX_SEGMENT_LENGTH, rem );

                    segments.Add( new Segment( j, len ) );

                    j += len;
                    rem -= len;

                } while( rem > 0 );
            }


            int show_pb_time = unchecked(Environment.TickCount + 333); // (ignore overflow)
            int last_i = segments.Count;

            if( pb != null )
            {
                ceh.Invoke( CancellationToken.None, ( ) => //...
                {
                    pb.Visibility = Visibility.Hidden;
                    pb.Maximum = last_i;
                } );
            }

            //var rnd = new Random( );
            //segments = segments.OrderBy( s => rnd.Next( ) ).ToList( ); // just for fun

            //...
            //Debug.WriteLine( $"Total segments: {segments.Count}" );

            for( int i = 0; i < last_i; )
            {
                if( reh.IsCancellationRequested ) return false;

                ceh.Invoke( CancellationToken.None, ( ) =>
                {
                    if( pb != null )
                    {
                        if( Environment.TickCount > show_pb_time )
                        {
                            pb.Value = i;
                            pb.Visibility = Visibility.Visible;
                        }
                    }

                    var end = Environment.TickCount + MAX_BLOCKING_TIME_MS;
                    //int dbg_i = i;//...
                    do
                    {
                        var segment = segments[i];
                        td.Range0F( segment.Index, segment.Length ).Style( styleInfo );

                    } while( ++i < last_i && Environment.TickCount < end );

                    //Debug.WriteLine( $"Subsegments: {i - dbg_i}" ); //...

                } );
            }

            return true;
        }


        // This seems to be too slow compared with ApplyStyle...
        [Obsolete( "Too slow. Try 'ApplyStyle'.", true )]
        public static void ClearProperties( CancellationToken ct, ChangeEventHelper ceh, ProgressBar pb, TextData td, IList<Segment> segments0 )
        {
            // split into smaller segments

            var segments = new List<(int index, int length)>( segments0.Count );

            foreach( var segment in segments0 )
            {
                int j = segment.Index;
                int rem = segment.Length;

                do
                {
                    ct.ThrowIfCancellationRequested( );

                    int len = Math.Min( MAX_SEGMENT_LENGTH, rem );

                    segments.Add( (j, len) );

                    j += len;
                    rem -= len;

                } while( rem > 0 );
            }


            int show_pb_time = unchecked(Environment.TickCount + 333); // (ignore overflow)
            int last_i = segments.Count;

            if( pb != null )
            {
                ceh.Invoke( ct, ( ) =>
                {
                    pb.Visibility = Visibility.Hidden;
                    pb.Maximum = last_i;
                } );
            }

            //var rnd = new Random( );
            //segments = segments.OrderBy( s => rnd.Next() ).ToList( ); // just for fun

            for( int i = 0; i < last_i; )
            {
                ct.ThrowIfCancellationRequested( );

                ceh.Invoke( ct, ( ) =>
                {
                    if( pb != null )
                    {
                        if( Environment.TickCount > show_pb_time )
                        {
                            pb.Value = i;
                            pb.Visibility = Visibility.Visible;
                        }
                    }

                    var end = Environment.TickCount + MAX_BLOCKING_TIME_MS;
                    do
                    {
                        ct.ThrowIfCancellationRequested( );

                        var segment = segments[i];
                        td.Range( segment.index, segment.length ).ClearAllProperties( );

                    } while( ++i < last_i && Environment.TickCount < end );
                } );
            }
        }


        public static void ApplyProperty( CancellationToken ct, ChangeEventHelper ceh, TextData td, IList<Segment> segments0, DependencyProperty property, object value )
        {
            // split into smaller segments

            var segments = new List<(int index, int length)>( segments0.Count );

            foreach( var segment in segments0 )
            {
                int j = segment.Index;
                int rem = segment.Length;

                do
                {
                    ct.ThrowIfCancellationRequested( );

                    int len = Math.Min( MAX_SEGMENT_LENGTH, rem );

                    segments.Add( (j, len) );

                    j += len;
                    rem -= len;

                } while( rem > 0 );
            }


            int last_i = segments.Count;

            for( int i = 0; i < last_i; )
            {
                ct.ThrowIfCancellationRequested( );

                ceh.Invoke( ct, ( ) =>
                {
                    var end = Environment.TickCount + MAX_BLOCKING_TIME_MS;
                    do
                    {
                        ct.ThrowIfCancellationRequested( );

                        var segment = segments[i];
                        td.Range( segment.index, segment.length ).ApplyPropertyValue( property, value );

                    } while( ++i < last_i && Environment.TickCount < end );
                } );
            }
        }


        public static void BringIntoViewInvoked( ICancellable cnc, RichTextBox rtb, TextPointer start, TextPointer end, bool fullHorizontalScrollIfInvisible )
        {
            Rect start_rect = start.GetCharacterRect( LogicalDirection.Forward ); // (relative)
            Rect end_rect = end.GetCharacterRect( LogicalDirection.Backward ); // (relative)

            Rect rect_to_bring; // (relative)

            bool is_multiline = end_rect.Bottom > start_rect.Bottom;

            if( !is_multiline )
            {
                rect_to_bring = Rect.Union( start_rect, end_rect ); // (including RTL texts)
            }
            else
            {
                rect_to_bring = start_rect;

                var max_time = Environment.TickCount + 111;

                for( TextPointer tp = start.GetNextInsertionPosition( LogicalDirection.Forward );
                    tp != null && tp.CompareTo( end ) <= 0;
                    tp = tp.GetNextInsertionPosition( LogicalDirection.Forward ) )
                {
                    if( cnc.IsCancellationRequested ) return;

                    Rect r = tp.GetCharacterRect( LogicalDirection.Forward );

                    rect_to_bring.Union( r );

                    if( Environment.TickCount > max_time )
                    {
                        r = end.GetCharacterRect( LogicalDirection.Forward );
                        rect_to_bring.Union( r );

                        break;
                    }
                }

            }

            if( cnc.IsCancellationRequested ) return;

            if( rect_to_bring.IsEmpty )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return;
            }

            BringIntoViewInvoked( rtb, rect_to_bring, isRectRelative: true, fullHorizontalScrollIfInvisible );
        }


        public static void BringIntoViewInvoked( RichTextBox rtb, Rect rect, bool isRectRelative, bool fullHorizontalScrollIfInvisible )
        {
            if( rect.IsEmpty )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                return;
            }

            Rect absolute_rect;
            Rect relative_rect;

            double ho = rtb.HorizontalOffset;
            double vo = rtb.VerticalOffset;

            if( isRectRelative )
            {
                relative_rect = rect;
                absolute_rect = Rect.Offset( rect, ho, vo );
            }
            else
            {
                relative_rect = Rect.Offset( rect, -ho, -vo );
                absolute_rect = rect;
            }

            Rect viewport = new( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) ); // (relative)

            Thickness padding = new( 4 );

            if( relative_rect.Bottom > viewport.Bottom - padding.Bottom )
            {
                vo = Math.Max( 0, absolute_rect.Bottom - viewport.Height + padding.Bottom );
                relative_rect = Rect.Offset( absolute_rect, -ho, -vo );
            }

            if( relative_rect.Top < viewport.Top + padding.Top )
            {
                vo = Math.Max( 0, absolute_rect.Top - padding.Top );
                relative_rect = Rect.Offset( absolute_rect, -ho, -vo );
            }

            if( relative_rect.Right > viewport.Right - padding.Right )
            {
                ho = Math.Max( 0, absolute_rect.Right - viewport.Width + padding.Right );
                relative_rect = Rect.Offset( absolute_rect, -ho, -vo );
            }

            if( fullHorizontalScrollIfInvisible )
            {
                if( relative_rect.Right < viewport.Left + padding.Left )
                {
                    ho = Math.Max( 0, absolute_rect.Right - rtb.ViewportWidth + padding.Right );
                }
                else if( relative_rect.Left < viewport.Left + padding.Left )
                {
                    ho = Math.Max( 0, absolute_rect.Left - padding.Left );
                }
            }
            else if( relative_rect.Left < viewport.Left + padding.Left )
            {
                ho = Math.Max( 0, absolute_rect.Left - padding.Left );
            }

            // ('BeginInvoke' is required to work around the problem of uncoloured matches in Text area)
            rtb.Dispatcher.BeginInvoke( DispatcherPriority.Background, new Action( ( ) =>
            {
                rtb.ScrollToVerticalOffset( vo );
                rtb.ScrollToHorizontalOffset( ho );
            } ) );
        }


        [Conditional( "DEBUG" )]
        public static void DbgValidateEol( string eol )
        {
            Debug.Assert( eol == "\r\n" || eol == "\n\r" || eol == "\r" || eol == "\n" );
        }


        [GeneratedRegex( @"\r\n|\n\r|\r|\n" )]
        private static partial Regex SplitLinesRegex( );
    }
}
