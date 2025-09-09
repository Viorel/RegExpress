using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
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
using RegExpressWPFNET.Code;
using RegExpressWPFNET.Controls;


namespace RegExpressWPFNET.Adorners
{
    sealed partial class WhitespaceAdorner : Adorner
    {
        readonly Brush WsBrush = Brushes.LightSeaGreen;
        readonly Pen TabPen = new( Brushes.LightSeaGreen, 1 );
        readonly Pen EolPen = new( Brushes.LightSeaGreen, 1 );
        readonly Pen EofPen = new( Brushes.LightSeaGreen, 1 );
        readonly Brush EofBrush = Brushes.Transparent;

        readonly char[] SpacesAndTabs = [' ', '\t']; // (For performance reasons, we only consider regular spaces)

        readonly ResumableLoop Loop;

        List<Rect> PositionsSpaces = [];
        List<Rect> PositionsTabs = [];
        List<Rect> PositionsEols = [];
        Rect PositionEof = Rect.Empty;
        bool mShowWhitespaces = false;
        int PreviousTextChangedTime = Environment.TickCount;

        internal bool IsDbgDisabled { get; set; } // (disable this adorner for debugging purposes)


        public WhitespaceAdorner( MyRichTextBox rtb ) : base( rtb )
        {
            WsBrush.Freeze( );
            TabPen.Freeze( );
            EolPen.Freeze( );
            EofPen.Freeze( );
            EofBrush.Freeze( );

            IsHitTestVisible = false;

            Rtb.TextChanged += Rtb_TextChanged;
            Rtb.AddHandler( ScrollViewer.ScrollChangedEvent, new RoutedEventHandler( Rtb_ScrollChanged ), true );

            Loop = new ResumableLoop( "WS Adorner", ThreadProc, 33, 33, 444 );
        }


        public void ShowWhiteSpaces( bool yes )
        {
            if( IsDbgDisabled ) return;

            lock( this )
            {
                mShowWhitespaces = yes;

                Loop.SignalWaitAndExecute( );
            }

            // switching to "No" is handled here; the thread does not deal with it

            if( !mShowWhitespaces )
            {
                lock( this )
                {
                    PositionsSpaces.Clear( );
                    PositionsTabs.Clear( );
                    PositionsEols.Clear( );
                    PositionEof = Rect.Empty;
                }

                DelayedInvalidateVisual( );
            }
        }


        MyRichTextBox Rtb => (MyRichTextBox)AdornedElement;


        private void Rtb_TextChanged( object sender, TextChangedEventArgs e )
        {
            if( IsDbgDisabled ) return;
            if( Rtb.ChangeEventHelper.IsInChange ) return;
            if( !mShowWhitespaces ) return;

            // invalidate some areas, but not too often

            if( Environment.TickCount - PreviousTextChangedTime > 77 )
            {
                MyRichTextBox rtb = Rtb;

                foreach( TextChange? change in e.Changes )
                {
                    TextPointer start = rtb.Document.ContentStart.GetPositionAtOffset( change.Offset );
                    if( start == null ) continue;

                    TextPointer end = start.GetPositionAtOffset( Math.Max( change.RemovedLength, change.AddedLength ) );
                    if( end == null ) continue;

                    var start_rect = start.GetCharacterRect( LogicalDirection.Forward );
                    var end_rect = end.GetCharacterRect( LogicalDirection.Backward );
                    var change_rect = Rect.Union( start_rect, end_rect );
                    if( change_rect.IsEmpty ) continue;

                    //
                    change_rect = new Rect( change_rect.Left, change_rect.Top, rtb.ViewportWidth, change_rect.Height );
                    change_rect.Offset( rtb.HorizontalOffset, rtb.VerticalOffset );

                    lock( this )
                    {
                        for( int i = 0; i < PositionsSpaces.Count; ++i )
                        {
                            Rect r = PositionsSpaces[i];
                            if( r.IntersectsWith( change_rect ) )
                            {
                                PositionsSpaces[i] = Rect.Empty;
                            }
                        }

                        for( int i = 0; i < PositionsEols.Count; ++i )
                        {
                            Rect r = PositionsEols[i];
                            if( r.IntersectsWith( change_rect ) )
                            {
                                PositionsEols[i] = Rect.Empty;
                            }
                        }

                        if( PositionEof.IntersectsWith( change_rect ) )
                        {
                            PositionEof = Rect.Empty;
                        }
                    }
                }

                InvalidateVisual( );
            }

            PreviousTextChangedTime = Environment.TickCount;

            Loop.SignalWaitAndExecute( );
        }


        private void Rtb_ScrollChanged( object sender, RoutedEventArgs e )
        {
            if( IsDbgDisabled ) return;
            if( Rtb.ChangeEventHelper.IsInChange ) return;
            if( !mShowWhitespaces ) return;

            InvalidateVisual( ); // to redraw what we already have, in new positions
            Loop.SignalWaitAndExecute( );
        }


        protected override void OnRender( DrawingContext drawingContext )
        {
            base.OnRender( drawingContext );  // (probably nothing)

            if( IsDbgDisabled ) return;
            if( !mShowWhitespaces ) return;

            var dc = drawingContext;
            var rtb = Rtb;
            var clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

            dc.PushClip( new RectangleGeometry( clip_rect ) );

            var t = new TranslateTransform( -rtb.HorizontalOffset, -rtb.VerticalOffset );
            dc.PushTransform( t );

            // make copies
            List<Rect> positions_spaces;
            List<Rect> positions_tabs;
            List<Rect> positions_eols;
            Rect position_eof;

            lock( this )
            {
                positions_spaces = PositionsSpaces.ToList( );
                positions_tabs = PositionsTabs.ToList( );
                positions_eols = PositionsEols.ToList( );
                position_eof = PositionEof;
            }

            foreach( var rect in positions_spaces )
            {
                if( !rect.IsEmpty ) DrawSpace( dc, rect );
            }

            foreach( var rect in positions_tabs )
            {
                if( !rect.IsEmpty ) DrawTab( dc, rect );
            }

            foreach( var rect in positions_eols )
            {
                if( !rect.IsEmpty ) DrawEol( dc, rect );
            }

            if( !position_eof.IsEmpty )
            {
                DrawEof( dc, position_eof );
            }

            dc.Pop( ); // (transform)
            dc.Pop( ); // (clip)
        }


        protected override void OnRenderSizeChanged( SizeChangedInfo sizeInfo )
        {
            base.OnRenderSizeChanged( sizeInfo );

            if( IsDbgDisabled ) return;

            DelayedInvalidateVisual( );
            Loop.SignalWaitAndExecute( );
        }


        void DelayedInvalidateVisual( )
        {
            Dispatcher.BeginInvoke( DispatcherPriority.Background, new Action( InvalidateVisual ) );
        }


        void DrawSpace( DrawingContext dc, Rect rect ) // draw a '·'
        {
            const int DOT_SIZE = 2;

            double x = rect.Left + rect.Width / 2;
            double y = Math.Floor( rect.Top + rect.Height / 2 - DOT_SIZE / 2 + 1 );
            Rect dot_rect = new Rect( x, y, DOT_SIZE, DOT_SIZE );

            dc.DrawRectangle( WsBrush, null, dot_rect );
        }


        void DrawTab( DrawingContext dc, Rect rect ) // draw a '→'
        {
            const int ARROW_WIDTH = 6;

            double half_pen = TabPen.Thickness / 2;

            double x = Math.Ceiling( rect.Left ) + half_pen;
            double y = Math.Ceiling( rect.Top + rect.Height / 2 + 1 ) - half_pen;

            dc.DrawLine( TabPen, new Point( x, y ), new Point( x + ARROW_WIDTH, y ) );
            dc.DrawLine( TabPen, new Point( x + ARROW_WIDTH / 2, y - ARROW_WIDTH / 2 ), new Point( x + ARROW_WIDTH, y ) );
            dc.DrawLine( TabPen, new Point( x + ARROW_WIDTH / 2, y + ARROW_WIDTH / 2 ), new Point( x + ARROW_WIDTH, y ) );
        }


        void DrawEol( DrawingContext dc, Rect eol_rect ) // draw a '↲' 
        {
            const int EOL_WIDTH = 6;

            double half_pen = EolPen.Thickness / 2;

            double x = Math.Ceiling( eol_rect.Left + 2 ) + half_pen;
            double y = Math.Ceiling( eol_rect.Top + eol_rect.Height / 2 + eol_rect.Height / 8 ) - half_pen;

            dc.DrawLine( EolPen, new Point( x, y ), new Point( x + EOL_WIDTH, y ) );
            dc.DrawLine( EolPen, new Point( x + EOL_WIDTH, y ), new Point( x + EOL_WIDTH, y - eol_rect.Height * 0.35 ) );
            dc.DrawLine( EolPen, new Point( x, y ), new Point( x + EOL_WIDTH / 2, y - EOL_WIDTH / 2 ) );
            dc.DrawLine( EolPen, new Point( x, y ), new Point( x + EOL_WIDTH / 2, y + EOL_WIDTH / 2 ) );
        }


        void DrawEof( DrawingContext dc, Rect rect ) // draw a '▯'
        {
            const double EOF_WIDTH = 4;

            double h = Math.Ceiling( rect.Height * 0.3 );
            double half_pen = EofPen.Thickness / 2;

            double x = Math.Ceiling( rect.Left + 2 ) + half_pen;
            double y = Math.Floor( rect.Top + ( rect.Height - h ) / 2 ) + 1 - half_pen;

            Rect eof_rect = new Rect( x, y, EOF_WIDTH, h );

            dc.DrawRectangle( EofBrush, EofPen, eof_rect );
        }


        void ThreadProc( ICancellable cnc )
        {
            if( !mShowWhitespaces ) return;

            var rtb = Rtb;
            TextData? td = null;
            Rect clip_rect = Rect.Empty;
            int top_index = 0;

            UITaskHelper.Invoke( rtb,
                ( ) =>
                {
                    td = null;

                    var start_doc = rtb.Document.ContentStart;
                    var end_doc = rtb.Document.ContentStart;

                    if( !start_doc.HasValidLayout || !end_doc.HasValidLayout ) return;

                    var td0 = rtb.GetTextData( null );

                    if( cnc.IsCancellationRequested ) return;

                    td = td0;
                    clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

                    TextPointer start_pointer = rtb.GetPositionFromPoint( new Point( 0, 0 ), snapToText: true ).GetLineStartPosition( -1, out int unused );
                    top_index = td.TextPointers.GetIndex( start_pointer, LogicalDirection.Backward );
                    if( top_index < 0 ) top_index = 0;
                } );

            if( cnc.IsCancellationRequested ) return;

            if( td != null )
            {
                CollectEols( cnc, td, clip_rect, top_index );
                if( cnc.IsCancellationRequested ) return;

                CollectEof( cnc, td, clip_rect, top_index );
                if( cnc.IsCancellationRequested ) return;

                CollectSpaces( cnc, td, clip_rect, top_index );
            }

        }


        bool CollectSpaces( ICancellable cnc, TextData td, Rect clipRect, int topIndex )
        {
            if( cnc.IsCancellationRequested ) return false;

            var rtb = Rtb;

            List<Rect> positions_spaces = new( );
            List<Rect> positions_tabs = new( );

            List<int> indices = new( );

            for( var i = td.Text.IndexOfAny( SpacesAndTabs, topIndex );
                i >= 0;
                i = td.Text.IndexOfAny( SpacesAndTabs, i + 1 ) )
            {
                if( cnc.IsCancellationRequested ) return false;

                indices.Add( i );
            }

            var intermediate_results1 = new List<(int index, Rect left, Rect right)>( );
            var intermediate_results2 = new List<(int index, Rect left, Rect right)>( );
            int current_i = 0;

            void do_things( )
            {
                Debug.Assert( !intermediate_results1.Any( ) );

                var end_time = Environment.TickCount + 22;
                do
                {
                    if( current_i >= indices.Count ) break;

                    if( cnc.IsCancellationRequested ) return;

                    var index = indices[current_i];
                    var tps = td.TextPointers.GetTextPointers( index, index + 1 );
                    var left = tps.Item1;
                    var right = tps.Item2;

                    var left_rect = left.GetCharacterRect( LogicalDirection.Forward );
                    var right_rect = right.GetCharacterRect( LogicalDirection.Backward );

                    intermediate_results1.Add( (index, left_rect, right_rect) );

                    if( left_rect.Top > clipRect.Bottom ) break;

                    ++current_i;

                } while( Environment.TickCount < end_time );
            }

            if( cnc.IsCancellationRequested ) return false;

            var d = UITaskHelper.BeginInvoke( rtb, do_things );

            for(; ; )
            {
                d.Wait( );

                if( cnc.IsCancellationRequested ) return false;

                (intermediate_results1, intermediate_results2) = (intermediate_results2, intermediate_results1);

                if( !intermediate_results2.Any( ) ) break;

                d = UITaskHelper.BeginInvoke( rtb, do_things );

                bool should_break = false;

                Debug.Assert( !Rtb.Dispatcher.CheckAccess( ) );

                foreach( var (index, left_rect, right_rect) in intermediate_results2 )
                {
                    if( cnc.IsCancellationRequested ) return false;

                    if( right_rect.Bottom < clipRect.Top ) continue;
                    if( left_rect.Top > clipRect.Bottom )
                    {
                        should_break = true;
                        break;
                    }

                    switch( td.Text[index] )
                    {
                    case '\t':
                        positions_tabs.Add( Rect.Offset( left_rect, rtb.HorizontalOffset, rtb.VerticalOffset ) );
                        break;

                    default: // (space)
                        var r = new Rect( left_rect.TopLeft, right_rect.BottomRight );
                        r.Offset( rtb.HorizontalOffset, rtb.VerticalOffset );
                        positions_spaces.Add( r );
                        break;
                    }
                }

                if( should_break ) break;

                intermediate_results2.Clear( );
            }

            if( cnc.IsCancellationRequested ) return false;

            lock( this )
            {
                PositionsSpaces = positions_spaces;
                PositionsTabs = positions_tabs;
            }

            DelayedInvalidateVisual( );

            return true;
        }


        bool CollectEols( ICancellable cnc, TextData td, Rect clip_rect, int top_index )
        {
            if( cnc.IsCancellationRequested ) return false;

            var rtb = Rtb;

            List<Rect> positions_eols = new( );

            // lines with no right-to-left segments

            var matches = EolRegex( ).Matches( td.Text );

            for( int i = 0; i < matches.Count; ++i )
            {
                if( cnc.IsCancellationRequested ) return false;

                int index = matches[i].Index;

                if( index < top_index ) continue;

                int previous_index = i == 0 ? 0 : matches[i - 1].Index;

                bool has_RTL = false;

                for( int k = previous_index; k < index; ++k )
                {
                    if( cnc.IsCancellationRequested ) return false;

                    if( UnicodeUtilities.IsRTL( td.Text[k] ) )
                    {
                        has_RTL = true;
                        break;
                    }
                }

                if( has_RTL )
                {
                    // RTL needs more navigation to find the rightmost X

                    Rect left_rect = Rect.Empty;
                    double max_x = double.NaN;

                    bool should_continue = false;
                    bool should_break = false;

                    UITaskHelper.Invoke( rtb,
                        ( ) =>
                        {
                            Debug.Assert( td.TextPointers.Doc.Parent == rtb );

                            TextPointer left = td.TextPointers.GetTextPointer( index );

                            left_rect = left.GetCharacterRect( LogicalDirection.Forward );

                            if( left_rect.Bottom < clip_rect.Top ) { should_continue = true; return; }
                            if( left_rect.Top > clip_rect.Bottom ) { should_break = true; return; }

                            max_x = left_rect.Left;

                            for( var tp = left.GetInsertionPosition( LogicalDirection.Backward ); ; )
                            {
                                if( cnc.IsCancellationRequested ) return;

                                tp = tp.GetNextInsertionPosition( LogicalDirection.Backward );
                                if( tp == null ) break;

                                // WORKAROUND for lines like "0ראל", when "0" is matched and highlighted
                                tp = tp.GetInsertionPosition( LogicalDirection.Forward );

                                var rect_b = tp.GetCharacterRect( LogicalDirection.Backward );
                                var rect_f = tp.GetCharacterRect( LogicalDirection.Forward );

                                if( cnc.IsCancellationRequested ) return;

                                if( rect_b.Bottom < left_rect.Top && rect_f.Bottom < left_rect.Top ) break;

                                if( rect_b.Bottom > left_rect.Top )
                                {
                                    if( max_x < rect_b.Left ) max_x = rect_b.Left;
                                }

                                if( rect_f.Bottom > left_rect.Top )
                                {
                                    if( max_x < rect_f.Left ) max_x = rect_f.Left;
                                }
                            }
                        } );

                    if( cnc.IsCancellationRequested ) return false;
                    if( should_continue ) continue;
                    if( should_break ) break;

                    Rect eol_rect = new( new Point( max_x, left_rect.Top ), left_rect.Size );
                    eol_rect.Offset( rtb.HorizontalOffset, rtb.VerticalOffset );

                    positions_eols.Add( eol_rect );
                }
                else
                {
                    // no RTL; quick answer

                    Rect eol_rect = Rect.Empty;

                    UITaskHelper.Invoke( rtb,
                        ( ) =>
                        {
                            Debug.Assert( td.TextPointers.Doc.Parent == rtb );

                            TextPointer left = td.TextPointers.GetTextPointer( index );

                            eol_rect = left.GetCharacterRect( LogicalDirection.Forward );
                        } );

                    if( eol_rect.Bottom < clip_rect.Top ) continue;
                    if( eol_rect.Top > clip_rect.Bottom ) break;

                    eol_rect.Offset( rtb.HorizontalOffset, rtb.VerticalOffset );

                    positions_eols.Add( eol_rect );
                }
            }

            if( cnc.IsCancellationRequested ) return false;

            lock( this )
            {
                PositionsEols = positions_eols;
            }

            DelayedInvalidateVisual( );

            return true;
        }


        bool CollectEof( ICancellable cnc, TextData td, Rect clip_rect, int top_index )
        {
            if( cnc.IsCancellationRequested ) return false;

            var rtb = Rtb;

            double max_x = double.NaN;
            Rect end_rect = Rect.Empty;

            UITaskHelper.Invoke( rtb,
                ( ) =>
                {
                    var end = rtb.Document.ContentEnd;
                    end_rect = end.GetCharacterRect( LogicalDirection.Forward ); // (no width)

                    if( end_rect.Bottom < clip_rect.Top || end_rect.Top > clip_rect.Bottom ) return;

                    max_x = end_rect.Left;

                    // if no RTL, then return a quick answer

                    var begin_line = end.GetLineStartPosition( 0 );
                    if( begin_line != null )
                    {
                        var r = new TextRange( begin_line, end );
                        var text = r.Text;
                        bool has_RTL = false;

                        for( int k = 0; k < text.Length; ++k )
                        {
                            if( cnc.IsCancellationRequested ) return;

                            if( UnicodeUtilities.IsRTL( text[k] ) )
                            {
                                has_RTL = true;
                                break;
                            }
                        }

                        if( !has_RTL )
                        {
                            return;
                        }
                    }

                    // we have RTL segments that need additional navigation to find the rightmost X

                    for( var tp = end; ; )
                    {
                        if( cnc.IsCancellationRequested ) return;

                        tp = tp.GetNextInsertionPosition( LogicalDirection.Backward );
                        if( tp == null ) break;

                        // WORKAROUND for lines like "0ראל", when "0" is matched and highlighted
                        tp = tp.GetInsertionPosition( LogicalDirection.Forward );

                        var rect = tp.GetCharacterRect( LogicalDirection.Forward );
                        if( rect.Bottom < end_rect.Bottom ) break;

                        if( max_x < rect.Left ) max_x = rect.Left;
                    }
                } );

            if( cnc.IsCancellationRequested ) return false;

            lock( this )
            {
                if( double.IsNaN( max_x ) )
                {
                    PositionEof = Rect.Empty;
                }
                else
                {
                    PositionEof = new Rect( new Point( max_x, end_rect.Top ), end_rect.Size );
                    PositionEof.Offset( rtb.HorizontalOffset, rtb.VerticalOffset );
                }
            }

            DelayedInvalidateVisual( );

            return true;
        }


        [GeneratedRegex( @"(?>\r\n|\n\r|\r|\n)", RegexOptions.ExplicitCapture | RegexOptions.IgnorePatternWhitespace )]
        private static partial Regex EolRegex( );
    }
}
