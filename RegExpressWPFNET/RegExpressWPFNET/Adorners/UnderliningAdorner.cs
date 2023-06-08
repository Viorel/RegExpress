using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Threading;


namespace RegExpressWPFNET.Adorners
{
    class UnderliningAdorner : Adorner
    {
        readonly Pen Pen = new( Brushes.Fuchsia, 1 );

        IReadOnlyList<(TextPointer start, TextPointer end)>? Ranges = null;

        GeometryGroup? mGeometryGroup = null;

        enum StateEnum
        {
            Restart,
            Continue,
            Done
        }

        // state data
        StateEnum mState = StateEnum.Done;
        int mNextIndex = 0;
        TextPointer? mNextTextPointer = null;
        LineGeometry? mCurrentLine = null;
        Point? mCurrentPrevPointF = null;
        GeometryGroup? mCurrentGeometryGroup = null;
        Size mLastScroll = Size.Empty;

        internal bool IsDbgDisabled { get; set; } // (disable this adorner for debugging purposes)


        public UnderliningAdorner( UIElement adornedElement ) : base( adornedElement )
        {
            Debug.Assert( adornedElement is RichTextBox );

            Pen.StartLineCap = PenLineCap.Round;
            Pen.EndLineCap = PenLineCap.Round;

            Pen.Freeze( );

            IsHitTestVisible = false;

            Rtb.TextChanged += Rtb_TextChanged;
            Rtb.AddHandler( ScrollViewer.ScrollChangedEvent, new RoutedEventHandler( Rtb_ScrollChanged ), true );

            mState = StateEnum.Done;
            ClearStateData( );
        }


        public void SetRangesToUnderline( IReadOnlyList<(TextPointer start, TextPointer end)>? ranges )
        {
            if( IsDbgDisabled ) return;

            lock( this )
            {
                if( ranges != null && Ranges != null && ranges.Count == Ranges.Count )
                {
                    bool are_different = false;

                    for( int i = 0; i < ranges.Count; ++i )
                    {
                        (TextPointer start, TextPointer end) r = ranges[i];
                        (TextPointer start, TextPointer end) R = Ranges[i];

                        if( !R.start.IsInSameDocument( r.start ) ||
                            (
                            !( r.start.CompareTo( R.start ) == 0 && ( r.end.CompareTo( R.end ) == 0 ) ) )
                            )
                        {
                            are_different = true;
                            break;
                        }
                    }

                    if( !are_different ) return;
                }

                Ranges = ranges;

                mState = StateEnum.Restart;
                ClearStateData( );
            }

            DelayedInvalidateVisual( );
        }


        RichTextBox Rtb => (RichTextBox)AdornedElement;


        private void Rtb_TextChanged( object sender, TextChangedEventArgs e )
        {
            mState = StateEnum.Restart;
            ClearStateData( );

            DelayedInvalidateVisual( );
        }


        private void Rtb_ScrollChanged( object sender, RoutedEventArgs e )
        {
            mState = StateEnum.Restart;
            ClearStateData( );

            if( mGeometryGroup != null )
            {
                TranslateTransform tr = new( mLastScroll.Width - Rtb.HorizontalOffset, mLastScroll.Height - Rtb.VerticalOffset );
                mGeometryGroup.Transform = tr;
            }

            InvalidateVisual( );
        }


        protected override void OnRender( DrawingContext drawingContext )
        {
            base.OnRender( drawingContext );  // (probably nothing)

            if( IsDbgDisabled ) return;

            lock( this )
            {
                const int TIMESLOT_MS = 77;

                if( mState == StateEnum.Done )
                {
                    DrawSegments( drawingContext, mGeometryGroup );

                    return;
                }

                if( mState == StateEnum.Restart )
                {
                    mState = StateEnum.Continue;
                    ClearStateData( );
                    mCurrentGeometryGroup = new( );

                    // (no return)
                }

                Debug.Assert( mState == StateEnum.Continue );

                if( Ranges == null || mNextIndex >= Ranges.Count )
                {
                    // no more ranges

                    mGeometryGroup = mCurrentGeometryGroup;
                    mLastScroll = new( Rtb.HorizontalOffset, Rtb.VerticalOffset );

                    mState = StateEnum.Done;
                    ClearStateData( );

                    DrawSegments( drawingContext, mGeometryGroup );

                    return;
                }

                if( Ranges != null )
                {
                    var rtb = Rtb;
                    var start_doc = rtb.Document.ContentStart;
                    var half_pen = Pen.Thickness / 2;
                    var clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

                    var start_time = Environment.TickCount64;

                    for( ; mNextIndex < Ranges.Count; ++mNextIndex )
                    {
                        (TextPointer start0, TextPointer end0) = Ranges[mNextIndex];

                        if( !start0.HasValidLayout || !end0.HasValidLayout ) continue;
                        if( !start0.IsInSameDocument( start_doc ) ) continue;

                        Debug.Assert( end0.IsInSameDocument( start0 ) );

                        var start = start0.GetInsertionPosition( LogicalDirection.Forward );
                        // next is needed to make it work for various cases of combining marks and bidirectional texts
                        var end = end0.GetInsertionPosition( LogicalDirection.Forward ).GetInsertionPosition( LogicalDirection.Backward );

                        //TextPointer end_b = end.GetInsertionPosition( LogicalDirection.Backward );
                        //TextPointer end_f = end.GetInsertionPosition( LogicalDirection.Forward );

                        Point start_point_b = start.GetCharacterRect( LogicalDirection.Backward ).BottomLeft;
                        Point start_point_f = start.GetCharacterRect( LogicalDirection.Forward ).BottomLeft;

                        Point end_point_b = end.GetCharacterRect( LogicalDirection.Backward ).BottomLeft;
                        Point end_point_f = end.GetCharacterRect( LogicalDirection.Forward ).BottomLeft;

                        const int CAPS_HEIGHT = 3;

                        if( start_point_b.Y <= clip_rect.Bottom + CAPS_HEIGHT &&
                            start_point_f.Y <= clip_rect.Bottom + CAPS_HEIGHT &&
                            end_point_b.Y >= clip_rect.Top &&
                            end_point_f.Y >= clip_rect.Top )
                        {
                            //Point prev_point_b = start_point_b;
                            Point prev_point_f = mCurrentPrevPointF ?? start_point_f;

                            for(
                                var tp = mNextTextPointer ?? start.GetNextInsertionPosition( LogicalDirection.Forward );
                                tp != null && tp.CompareTo( end ) <= 0;
                                tp = tp.GetNextInsertionPosition( LogicalDirection.Forward )
                                )
                            {
                                Point tp_point_b = tp.GetCharacterRect( LogicalDirection.Backward ).BottomLeft;
                                Point tp_point_f = tp.GetCharacterRect( LogicalDirection.Forward ).BottomLeft;

                                Point p1 = prev_point_f;
                                Point p2 = tp_point_b;

                                p1.Y = Math.Ceiling( p1.Y ) - half_pen;
                                p2.Y = Math.Ceiling( p2.Y ) - half_pen;

                                if( p2.Y != p1.Y ) //
                                {
                                    // transient case, text was just edited;
                                    // will receive new segments in few moments
                                }
                                else
                                {
                                    if( p1.Y > clip_rect.Bottom + half_pen ) break; // already invisible

                                    if( p1.Y < clip_rect.Top - half_pen )
                                    {
                                        // not visible yet
                                    }
                                    else
                                    {
                                        // try combining with current line, only most probable cases;
                                        // '==' operator seems to work

                                        bool combined = false;

                                        if( mCurrentLine != null )
                                        {
                                            if( mCurrentLine.StartPoint.Y == p1.Y )
                                            {
                                                if( mCurrentLine.StartPoint.X < mCurrentLine.EndPoint.X &&
                                                    p1.X < p2.X &&
                                                    mCurrentLine.EndPoint.X == p1.X )
                                                {
                                                    mCurrentLine.EndPoint = p2;
                                                    combined = true;
                                                }
                                                else if( mCurrentLine.EndPoint.X < mCurrentLine.StartPoint.X &&
                                                    p2.X < p1.X &&
                                                    mCurrentLine.EndPoint.X == p1.X )
                                                {
                                                    mCurrentLine.EndPoint = p2;
                                                    combined = true;
                                                }
                                            }
                                        }

                                        if( !combined )
                                        {
                                            if( mCurrentLine != null )
                                            {
                                                mCurrentLine.Freeze( );
                                                mCurrentGeometryGroup!.Children.Add( mCurrentLine );
                                            }

                                            mCurrentLine = new LineGeometry( p1, p2 );
                                        }
                                    }
                                }

                                prev_point_f = tp_point_f;
                                //prev_point_b = tp_point_b;

                                if( Environment.TickCount64 - start_time >= TIMESLOT_MS )
                                {
                                    DrawSegments( drawingContext, mGeometryGroup );
                                    DrawSegments( drawingContext, mCurrentGeometryGroup );
                                    mNextTextPointer = tp.GetNextInsertionPosition( LogicalDirection.Forward );
                                    mCurrentPrevPointF = prev_point_f;
                                    DelayedInvalidateVisual( );

                                    return;
                                }

                            } // for tp

                            if( mCurrentLine != null )
                            {
                                mCurrentLine.Freeze( );
                                mCurrentGeometryGroup!.Children.Add( mCurrentLine );
                                mCurrentLine = null;
                            }
                        }

                        {
                            // line caps

                            var x = start_point_f.X;
                            var y = start_point_f.Y - half_pen;
                            var start_cap = new LineGeometry( new Point( x, y ), new Point( x, y - CAPS_HEIGHT ) );
                            start_cap.Freeze( );

                            mCurrentGeometryGroup!.Children.Add( start_cap );

                            if( mCurrentGeometryGroup!.Children.Count > 1 ) // (i.e. has horizontal lines too)
                            {
                                x = end_point_b.X;
                                y = end_point_b.Y - half_pen;
                                var end_cap = new LineGeometry( new Point( x, y ), new Point( x, y - CAPS_HEIGHT ) );
                                end_cap.Freeze( );

                                mCurrentGeometryGroup!.Children.Add( end_cap );
                            }
                        }

                        if( Environment.TickCount64 - start_time >= TIMESLOT_MS )
                        {
                            DrawSegments( drawingContext, mGeometryGroup );
                            DrawSegments( drawingContext, mCurrentGeometryGroup );
                            ++mNextIndex;
                            mNextTextPointer = null;
                            mCurrentLine = null;
                            mCurrentPrevPointF = null;
                            DelayedInvalidateVisual( );

                            return;
                        }
                    } // for mNextIndex
                }

                // no more ranges

                mGeometryGroup = mCurrentGeometryGroup;
                mLastScroll = new( Rtb.HorizontalOffset, Rtb.VerticalOffset );

                mState = StateEnum.Done;
                ClearStateData( );

                DrawSegments( drawingContext, mGeometryGroup );
            }
        }


        void ClearStateData( )
        {
            mNextIndex = 0;
            mNextTextPointer = null;
            mCurrentLine = null;
            mCurrentPrevPointF = null;
            mCurrentGeometryGroup = null;
        }


        void DrawSegments( DrawingContext drawingContext, GeometryGroup? geometryGroup )
        {
            if( geometryGroup == null ) return;

            var rtb = Rtb;
            Rect clip_rect = new( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

            drawingContext.PushClip( new RectangleGeometry( clip_rect ) );
            drawingContext.DrawGeometry( brush: null, pen: Pen, geometry: geometryGroup );
            drawingContext.Pop( );
        }


        void DelayedInvalidateVisual( )
        {
            Dispatcher.BeginInvoke( DispatcherPriority.Background, new Action( InvalidateVisual ) );
        }

    }
}
