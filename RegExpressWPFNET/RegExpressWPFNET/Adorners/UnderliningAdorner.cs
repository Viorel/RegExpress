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

		readonly GeometryGroup GeometryGroup = new( );
		bool MustRecalculateSegments = true;

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

					if( !are_different )
					{
						return;
					}
				}

				Ranges = ranges;
				MustRecalculateSegments = true;
			}

			DelayedInvalidateVisual( );
		}


        RichTextBox Rtb => (RichTextBox)AdornedElement;


        private void Rtb_TextChanged( object sender, TextChangedEventArgs e )
		{
			MustRecalculateSegments = true;
			DelayedInvalidateVisual( );
		}


		private void Rtb_ScrollChanged( object sender, RoutedEventArgs e )
		{
			MustRecalculateSegments = true;
			InvalidateVisual( );
		}


		protected override void OnRender( DrawingContext drawingContext )
		{
			base.OnRender( drawingContext );  // (probably nothing)

			if( IsDbgDisabled ) return;

			lock( this )
			{
				if( MustRecalculateSegments )
				{
					RecalculateSegments( );
				}

				var rtb = Rtb;
				var dc = drawingContext;
				var clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

				dc.PushClip( new RectangleGeometry( clip_rect ) );

				dc.DrawGeometry( null, Pen, GeometryGroup );

				dc.Pop( );

				MustRecalculateSegments = false;
			}
		}


		void RecalculateSegments( )
		{
			GeometryGroup.Children.Clear( );

			if( Ranges == null ) return;

			var rtb = Rtb;

			var clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

			var start_doc = rtb.Document.ContentStart;
			var half_pen = Pen.Thickness / 2;

			// TODO: clean 'Ranges' if document was changed (release old document), in thread-safe manner

			foreach( var (start0, end0) in Ranges )
			{
				if( start0.HasValidLayout && end0.HasValidLayout )
				{
					if( start0.IsInSameDocument( start_doc ) )
					{
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
							Point prev_point_b = start_point_b;
							Point prev_point_f = start_point_f;

							LineGeometry? current_line = null;

							for(
								var tp = start.GetNextInsertionPosition( LogicalDirection.Forward );
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

										if( current_line != null )
										{
											if( current_line.StartPoint.Y == p1.Y )
											{
												if( current_line.StartPoint.X < current_line.EndPoint.X &&
													p1.X < p2.X &&
													current_line.EndPoint.X == p1.X )
												{
													current_line.EndPoint = p2;
													combined = true;
												}
												else if( current_line.EndPoint.X < current_line.StartPoint.X &&
													p2.X < p1.X &&
													current_line.EndPoint.X == p1.X )
												{
													current_line.EndPoint = p2;
													combined = true;
												}
											}
										}

										if( !combined )
										{
											if( current_line != null )
											{
												current_line.Freeze( );
												GeometryGroup.Children.Add( current_line );
											}

											current_line = new LineGeometry( p1, p2 );
										}
									}
								}

								prev_point_f = tp_point_f;
								prev_point_b = tp_point_b;
							}

							if( current_line != null )
							{
								current_line.Freeze( );
								GeometryGroup.Children.Add( current_line );
							}
						}

						{
							// line caps

							var x = start_point_f.X;
							var y = start_point_f.Y - half_pen;

							GeometryGroup.Children.Add( new LineGeometry( new Point( x, y ), new Point( x, y - CAPS_HEIGHT ) ) );

							if( GeometryGroup.Children.Count > 1 ) // (i.e. has horisontal lines too)
							{
								x = end_point_b.X;
								y = end_point_b.Y - half_pen;

								GeometryGroup.Children.Add( new LineGeometry( new Point( x, y ), new Point( x, y - CAPS_HEIGHT ) ) );
							}
						}
					}
				}
			}
		}


		void DelayedInvalidateVisual( )
		{
			Dispatcher.BeginInvoke( DispatcherPriority.Background, new Action( InvalidateVisual ) );
		}

	}
}
