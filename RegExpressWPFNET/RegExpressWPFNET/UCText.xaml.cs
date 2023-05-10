using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Media;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Windows.Threading;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using RegExpressWPFNET.Adorners;
using RegExpressWPFNET.Code;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for UCText.xaml
    /// </summary>
    partial class UCText : UserControl, IDisposable
    {
        readonly WhitespaceAdorner WhitespaceAdorner;
        readonly UnderliningAdorner LocalUnderliningAdorner;
        readonly UnderliningAdorner ExternalUnderliningAdorner;

        readonly ResumableLoop RecolouringLoop;
        readonly ResumableLoop LocalUnderliningLoop;
        readonly ResumableLoop ExternalUnderliningLoop;
        readonly ManualResetEvent MatchesUpdatedEvent = new ManualResetEvent( initialState: false );

        readonly ChangeEventHelper ChangeEventHelper;
        readonly UndoRedoHelper UndoRedoHelper;

        bool AlreadyLoaded = false;

        string? LastText;
        RegexMatches? LastMatches;
        bool LastShowCaptures;
        string? LastEol;
        bool LastPotentialOverlaps;
        UnderlineInfo? LastExternalUnderlineInfo;
        bool LastExternalUnderlineSetSelection;
        bool LastNoGroupDetails;

        readonly StyleInfo NormalStyleInfo;
        readonly StyleInfo[] HighlightStyleInfos;

        public event EventHandler? TextChanged;
        public event EventHandler? SelectionChanged;
        public event EventHandler? LocalUnderliningFinished;


        public UCText( )
        {
            InitializeComponent( );

            ChangeEventHelper = new ChangeEventHelper( this.rtb );
            UndoRedoHelper = new UndoRedoHelper( this.rtb );

            WhitespaceAdorner = new WhitespaceAdorner( rtb, ChangeEventHelper );
            LocalUnderliningAdorner = new UnderliningAdorner( rtb );
            ExternalUnderliningAdorner = new UnderliningAdorner( rtb );

            NormalStyleInfo = new StyleInfo( "TextNormal" );

            HighlightStyleInfos = new[]
            {
                new StyleInfo( "MatchHighlight_0" ),
                new StyleInfo( "MatchHighlight_1" ),
                new StyleInfo( "MatchHighlight_2" )
            };


            RecolouringLoop = new ResumableLoop( RecolouringThreadProc, 333, 555 );
            LocalUnderliningLoop = new ResumableLoop( LocalUnderliningThreadProc, 222, 444 );
            ExternalUnderliningLoop = new ResumableLoop( ExternalUnderliningThreadProc, 333, 555 );


            pnlDebug.Visibility = Visibility.Collapsed;
#if !DEBUG
			pnlDebug.Visibility = Visibility.Collapsed;
#endif
            //WhitespaceAdorner.IsDbgDisabled = true;
            //LocalUnderliningAdorner.IsDbgDisabled = true;
            //ExternalUnderliningAdorner.IsDbgDisabled = true;
        }


        public void Shutdown( )
        {
            TerminateAll( );
        }


        public BaseTextData GetBaseTextData( string eol )
        {
            return rtb.GetBaseTextData( eol );
        }


        public TextData GetTextData( string eol )
        {
            return rtb.GetTextData( eol );
        }


        public void SetText( string? value )
        {
            RtbUtilities.SetText( rtb, value );

            UndoRedoHelper.Init( );
        }


        public void SetMatches( RegexMatches matches, bool showCaptures, string eol, bool potentialOverlaps, bool noGroupDetails )
        {
            if( matches == null ) throw new ArgumentNullException( nameof( matches ) );

            string? last_text;
            RegexMatches? last_matches;
            bool last_show_captures;
            string? last_eol;
            bool last_potential_overlaps;
            bool last_no_group_details;

            lock( this )
            {
                last_text = LastText;
                last_matches = LastMatches;
                last_show_captures = LastShowCaptures;
                last_eol = LastEol;
                last_potential_overlaps = LastPotentialOverlaps;
                last_no_group_details = LastNoGroupDetails;
            }

            string text = GetBaseTextData( eol ).Text;

            if( last_matches != null )
            {
                var old_groups = last_matches.Matches.SelectMany( m => m.Groups ).Select( g => (g.Index, g.Length, g.Value) );
                var new_groups = matches.Matches.SelectMany( m => m.Groups ).Select( g => (g.Index, g.Length, g.Value) );

                if( string.Equals( text, last_text ) &&
                    showCaptures == last_show_captures &&
                    eol == last_eol &&
                    potentialOverlaps == last_potential_overlaps &&
                    noGroupDetails == last_no_group_details &&
                    new_groups.SequenceEqual( old_groups ) )
                {
                    lock( this )
                    {
                        LastMatches = matches;
                        LastExternalUnderlineInfo = null;
                    }

                    MatchesUpdatedEvent.Set( );

                    return;
                }
            }

            RecolouringLoop.SignalRewind( );
            LocalUnderliningLoop.SignalRewind( );
            ExternalUnderliningLoop.SignalRewind( );

            lock( this )
            {
                LastText = text;
                LastMatches = matches;
                LastShowCaptures = showCaptures;
                LastEol = eol;
                LastPotentialOverlaps = potentialOverlaps;
                LastExternalUnderlineInfo = null;
                LastNoGroupDetails = noGroupDetails;
            }

            MatchesUpdatedEvent.Set( );

            RecolouringLoop.SignalWaitAndExecute( );
            LocalUnderliningLoop.SignalWaitAndExecute( );
            ExternalUnderliningLoop.SignalWaitAndExecute( );
        }


        public void ShowWhiteSpaces( bool yes )
        {
            WhitespaceAdorner.ShowWhiteSpaces( yes );
        }


        public IReadOnlyList<Segment> GetUnderliningInfo( )
        {
            if( LastMatches == null )
            {
                return Enumerable.Empty<Segment>( ).ToList( );
            }

            TextData? td = null;

            if( !CheckAccess( ) )
            {
                ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                {
                    td = rtb.GetTextData( LastEol );
                } );
            }
            else
            {
                td = rtb.GetTextData( LastEol );
            }

            return GetUnderliningInfo( NonCancellable.Instance, td!, LastMatches, LastShowCaptures, LastNoGroupDetails );
        }


        public void SetExternalUnderlining( UnderlineInfo underlineInfo, bool setSelection )
        {
            ExternalUnderliningLoop.SignalRewind( );

            lock( this )
            {
                LastExternalUnderlineInfo = underlineInfo;
                LastExternalUnderlineSetSelection = setSelection;
            }

            ExternalUnderliningLoop.SignalWaitAndExecute( );
        }


        public void StopAll( )
        {
            RecolouringLoop.SignalRewind( );
            LocalUnderliningLoop.SignalRewind( );
            ExternalUnderliningLoop.SignalRewind( );
        }


        public void TerminateAll( )
        {
            RecolouringLoop.Terminate( );
            LocalUnderliningLoop.Terminate( );
            ExternalUnderliningLoop.Terminate( );
        }


        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( AlreadyLoaded ) return;

            rtb.Document.MinPageWidth = Utilities.ToPoints( "21cm" );

            var adorner_layer = AdornerLayer.GetAdornerLayer( rtb );
            adorner_layer.Add( WhitespaceAdorner );
            adorner_layer.Add( LocalUnderliningAdorner );
            adorner_layer.Add( ExternalUnderliningAdorner );

            AlreadyLoaded = true;
        }


        private void Rtb_SelectionChanged( object sender, RoutedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( ChangeEventHelper.IsInChange ) return;
            if( !rtb.IsFocused ) return;

            LocalUnderliningLoop.SignalWaitAndExecute( );

            UndoRedoHelper.HandleSelectionChanged( );

            SelectionChanged?.Invoke( this, EventArgs.Empty );

            ShowDebugInformation( ); // #if DEBUG
        }


        private void Rtb_TextChanged( object sender, TextChangedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( ChangeEventHelper.IsInChange ) return;

            RecolouringLoop.SignalRewind( );
            LocalUnderliningLoop.SignalRewind( );
            ExternalUnderliningLoop.SignalRewind( );

            UndoRedoHelper.HandleTextChanged( e );

            //...
            //lock( this )
            //{
            //	LastMatches = null;
            //	LastShowCaptures = false;
            //	LastEol = null;
            //}

            MatchesUpdatedEvent.Reset( );

            TextChanged?.Invoke( this, EventArgs.Empty );
        }


        private void Rtb_ScrollChanged( object sender, ScrollChangedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( ChangeEventHelper.IsInChange ) return;

            RecolouringLoop.SignalWaitAndExecute( );
        }


        private void Rtb_SizeChanged( object sender, SizeChangedEventArgs e )
        {
            if( !AlreadyLoaded ) return;
            if( ChangeEventHelper.IsInChange ) return;

            RecolouringLoop.SignalWaitAndExecute( );
        }


        private void Rtb_GotFocus( object sender, RoutedEventArgs e )
        {
            LocalUnderliningLoop.SignalWaitAndExecute( );
            ExternalUnderliningLoop.SignalWaitAndExecute( );

            if( Properties.Settings.Default.BringCaretIntoView )
            {
                if( rtb.CaretPosition.Parent is FrameworkContentElement p )
                {
                    p.BringIntoView( );
                }
            }
        }


        private void Rtb_LostFocus( object sender, RoutedEventArgs e )
        {
            LocalUnderliningLoop.SignalWaitAndExecute( );
        }


        private void Rtb_Pasting( object sender, DataObjectPastingEventArgs e )
        {
            if( e.DataObject.GetDataPresent( DataFormats.UnicodeText ) )
            {
                e.FormatToApply = DataFormats.UnicodeText;
            }
            else if( e.DataObject.GetDataPresent( DataFormats.Text ) )
            {
                e.FormatToApply = DataFormats.Text;
            }
            else
            {
                e.CancelCommand( );
            }
        }


        private void BtnDbgSave_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            rtb.Focus( );

            Utilities.DbgSaveXAML( @"debug-uctext.xml", rtb.Document );

            SaveToPng( Window.GetWindow( this ), "debug-uctext.png" );
#endif
        }


        private void BtnDbgInsertB_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            var p = rtb.Selection.Start.GetInsertionPosition( LogicalDirection.Backward );
            if( p == null )
            {
                SystemSounds.Beep.Play( );
            }
            else
            {
                rtb.Selection.Select( p, p );
                rtb.Focus( );
            }
#endif
        }

        private void BtnDbgInsertF_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            var p = rtb.Selection.Start.GetInsertionPosition( LogicalDirection.Forward );
            if( p == null )
            {
                SystemSounds.Beep.Play( );
            }
            else
            {
                rtb.Selection.Select( p, p );
                rtb.Focus( );
            }
#endif
        }


        private void BtnDbgNextInsert_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            var p = rtb.Selection.Start.GetNextInsertionPosition( LogicalDirection.Forward );
            if( p == null )
            {
                SystemSounds.Beep.Play( );
            }
            else
            {
                rtb.Selection.Select( p, p );
                rtb.Focus( );
            }
#endif
        }

        private void BtnDbgNextContext_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            var p = rtb.Selection.Start.GetNextContextPosition( LogicalDirection.Forward );
            if( p == null )
            {
                SystemSounds.Beep.Play( );
            }
            else
            {
                rtb.Selection.Select( p, p );
                rtb.Focus( );
            }
#endif
        }


#if DEBUG
        // https://blogs.msdn.microsoft.com/kirillosenkov/2009/10/12/saving-images-bmp-png-etc-in-wpfsilverlight/
        void SaveToPng( FrameworkElement visual, string fileName )
        {
            var encoder = new PngBitmapEncoder( );
            SaveUsingEncoder( visual, fileName, encoder );
        }

        static void SaveUsingEncoder( FrameworkElement visual, string fileName, BitmapEncoder encoder )
        {
            RenderTargetBitmap bitmap = new(
                (int)visual.ActualWidth,
                (int)visual.ActualHeight,
                96,
                96,
                PixelFormats.Pbgra32 );
            bitmap.Render( visual );
            BitmapFrame frame = BitmapFrame.Create( bitmap );
            encoder.Frames.Add( frame );

            using( var stream = File.Create( fileName ) )
            {
                encoder.Save( stream );
            }
        }
#endif


        void RecolouringThreadProc( ICancellable cnc )
        {
            RegexMatches? matches;
            string? eol;
            bool show_captures;
            bool potential_overlaps;

            lock( this )
            {
                matches = LastMatches;
                eol = LastEol;
                show_captures = LastShowCaptures;
                potential_overlaps = LastPotentialOverlaps;
            }

            TextData? td = null;
            Rect clip_rect = Rect.Empty;
            int top_index = 0;
            int bottom_index = 0;

            UITaskHelper.Invoke( rtb, ( ) =>
            {
                td = null;

                var start_doc = rtb.Document.ContentStart;
                var end_doc = rtb.Document.ContentStart;

                if( !start_doc.HasValidLayout || !end_doc.HasValidLayout ) return;

                var td0 = rtb.GetTextData( eol );

                if( cnc.IsCancellationRequested ) return;

                td = td0;
                clip_rect = new Rect( new Size( rtb.ViewportWidth, rtb.ViewportHeight ) );

                TextPointer top_pointer = rtb.GetPositionFromPoint( new Point( 0, 0 ), snapToText: true ).GetLineStartPosition( -1, out int _ );
                if( cnc.IsCancellationRequested ) return;

                top_index = td.TextPointers.GetIndex( top_pointer, LogicalDirection.Backward );
                if( cnc.IsCancellationRequested ) return;
                if( top_index < 0 ) top_index = 0;

                TextPointer bottom_pointer = rtb.GetPositionFromPoint( new Point( 0, rtb.ViewportHeight ), snapToText: true ).GetLineStartPosition( +1, out int lines_skipped );
                if( cnc.IsCancellationRequested ) return;

                if( bottom_pointer == null || lines_skipped == 0 )
                {
                    bottom_index = td.Text.Length;
                }
                else
                {
                    bottom_index = td.TextPointers.GetIndex( bottom_pointer, LogicalDirection.Forward );
                    if( cnc.IsCancellationRequested ) return;
                }
                if( bottom_index > td.Text.Length ) bottom_index = td.Text.Length;
                if( bottom_index < top_index ) bottom_index = top_index; // (including 'if bottom_index == 0')
            } );

            if( cnc.IsCancellationRequested ) return;

            if( td == null ) return;
            if( td.Text.Length == 0 ) return;

            Debug.Assert( top_index >= 0 );
            Debug.Assert( bottom_index >= top_index );
            Debug.Assert( bottom_index <= td.Text.Length );

            // (NOTE. Overlaps are possible in this example: (?=(..))

            var segments_and_styles = new List<(Segment segment, StyleInfo styleInfo)>( );
            var segments_to_uncolour = new List<Segment>
            {
                new Segment( top_index, bottom_index - top_index + 1 )
            };

            if( matches != null && matches.Count > 0 )
            {
                int i = -1;
                foreach( var match in matches.Matches )
                {
                    ++i;

                    if( cnc.IsCancellationRequested ) break;

                    Debug.Assert( match.Success );

                    // TODO: consider these conditions for bi-directional text
                    if( match.TextIndex + match.TextLength < top_index ) continue;
                    if( match.TextIndex > bottom_index ) continue; // (do not break; the order of indices is unspecified)

                    var highlight_index = unchecked(i % HighlightStyleInfos.Length);

                    Segment.Except( segments_to_uncolour, match.TextIndex, match.TextLength );
                    segments_and_styles.Add( (new Segment( match.TextIndex, match.TextLength ), HighlightStyleInfos[highlight_index]) );
                }
            }

            if( cnc.IsCancellationRequested ) return;

            List<(Segment segment, StyleInfo styleInfo)> segments_to_uncolour_with_style =
                            segments_to_uncolour
                                .Select( s => (s, NormalStyleInfo) )
                                .ToList( );

            if( cnc.IsCancellationRequested ) return;

            int center_index = ( top_index + bottom_index ) / 2;

            var all_segments_and_styles_e = segments_and_styles.Concat( segments_to_uncolour_with_style );
            if( !potential_overlaps ) all_segments_and_styles_e = all_segments_and_styles_e.OrderBy( s => Math.Abs( center_index - ( s.segment.Index + s.segment.Length / 2 ) ) );

            if( cnc.IsCancellationRequested ) return;

            var all_segments_and_styles = all_segments_and_styles_e.ToList( );

            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, pbProgress, td, all_segments_and_styles );

            if( cnc.IsCancellationRequested ) return;

            UITaskHelper.BeginInvoke( pbProgress, ( ) =>
                        {
                            pbProgress.Visibility = Visibility.Hidden;
                        } );
        }


        void LocalUnderliningThreadProc( ICancellable cnc )
        {
            // if matches are outdated, then wait a while
            MatchesUpdatedEvent.WaitOne( 444 );

            bool is_focussed = false;
            RegexMatches? matches;
            string? eol;
            bool show_captures;
            bool no_group_details;

            lock( this )
            {
                matches = LastMatches;
                eol = LastEol;
                show_captures = LastShowCaptures;
                no_group_details = LastNoGroupDetails;
            }

            if( matches == null ) return;

            TextData? td = null;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                is_focussed = rtb.IsFocused;
                if( is_focussed ) td = rtb.GetTextData( eol );
            } );

            if( cnc.IsCancellationRequested ) return;

            List<Segment>? segments_to_underline = null;

            if( is_focussed )
            {
                segments_to_underline = GetUnderliningInfo( cnc, td!, matches, show_captures, no_group_details ).ToList( );
            }

            if( cnc.IsCancellationRequested ) return;

            IReadOnlyList<(TextPointer start, TextPointer end)>? ranges_to_underline = null;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
             {
                 ranges_to_underline =
                                 segments_to_underline
                                     ?.Select( s =>
                                     {
                                         var t = td!.TextPointers.GetTextPointers( s.Index, s.Index + s.Length );
                                         return (t.Item1, t.Item2);
                                     } )
                                     ?.ToList( );
             } );

            if( cnc.IsCancellationRequested ) return;

            LocalUnderliningAdorner.SetRangesToUnderline( ranges_to_underline );

            if( cnc.IsCancellationRequested ) return;

            if( is_focussed )
            {
                ChangeEventHelper.BeginInvoke( CancellationToken.None, ( ) =>
                            {
                                LocalUnderliningFinished?.Invoke( this, EventArgs.Empty );
                            } );
            }
        }


        void ExternalUnderliningThreadProc( ICancellable cnc )
        {
            string? eol;
            UnderlineInfo? underline_info;
            bool set_selection;

            lock( this )
            {
                eol = LastEol;
                underline_info = LastExternalUnderlineInfo;
                set_selection = LastExternalUnderlineSetSelection;
            }

            TextData? td = null;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                td = rtb.GetTextData( eol );
            } );

            if( cnc.IsCancellationRequested ) return;

            IReadOnlyList<(TextPointer start, TextPointer end)>? ranges_to_underline = null;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                ranges_to_underline =
                                underline_info?.Segments
                                    ?.Select( s =>
                                    {
                                        var t = td!.TextPointers.GetTextPointers( s.Index, s.Index + s.Length );
                                        return (t.Item1, t.Item2);
                                    } )
                                    ?.ToList( );
            } );

            if( cnc.IsCancellationRequested ) return;

            ExternalUnderliningAdorner.SetRangesToUnderline( ranges_to_underline );

            if( cnc.IsCancellationRequested ) return;

            if( underline_info?.Segments?.Count > 0 )
            {
                ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                {
                    var first = underline_info.Segments.First( );
                    var range = td!.Range( first.Index, first.Length );

                    RtbUtilities.BringIntoViewInvoked( cnc, rtb, range.Start, range.End, fullHorizontalScrollIfInvisible: true );

                    if( set_selection && !rtb.IsKeyboardFocused )
                    {
                        TextPointer p = range.Start.GetInsertionPosition( LogicalDirection.Forward );
                        rtb.Selection.Select( p, p );
                    }
                } );
            }
        }


        static IReadOnlyList<Segment> GetUnderliningInfo( ICancellable reh, TextData td, RegexMatches matches, bool showCaptures, bool noGroupDetails )
        {
            var items = new List<Segment>( );

            // include captures and groups; if no such objects, then include matches

            foreach( IMatch match in matches.Matches )
            {
                if( reh.IsCancellationRequested ) break;

                if( !match.Success ) continue;

                bool found = false;

                if( !noGroupDetails )
                {
                    foreach( IGroup group in match.Groups.Skip( 1 ) )
                    {
                        if( reh.IsCancellationRequested ) break;

                        if( !group.Success ) continue;

                        if( showCaptures )
                        {
                            foreach( ICapture capture in group.Captures )
                            {
                                if( reh.IsCancellationRequested ) break;

                                if( td.SelectionStart >= capture.TextIndex && td.SelectionStart <= capture.TextIndex + capture.TextLength )
                                {
                                    items.Add( new Segment( capture.TextIndex, capture.TextLength ) );
                                    found = true;
                                }
                            }
                        }

                        if( td.SelectionStart >= group.TextIndex && td.SelectionStart <= group.TextIndex + group.TextLength )
                        {
                            items.Add( new Segment( group.TextIndex, group.TextLength ) );
                            found = true;
                        }
                    }
                }

                if( !found )
                {
                    if( td.SelectionStart >= match.TextIndex && td.SelectionStart <= match.TextIndex + match.TextLength )
                    {
                        items.Add( new Segment( match.TextIndex, match.TextLength ) );
                    }
                }
            }

            return items;
        }


        [Conditional( "DEBUG" )]
        private void ShowDebugInformation( )
        {
            string s = "";

            TextPointer start = rtb.Selection.Start;

            Rect rectB = start.GetCharacterRect( LogicalDirection.Backward );
            Rect rectF = start.GetCharacterRect( LogicalDirection.Forward );

            s += $"BPos: {(int)rectB.Left}×{rectB.Bottom}, FPos: {(int)rectF.Left}×{rectF.Bottom}";

            char[] bc = new char[1];
            char[] fc = new char[1];

            int bn = start.GetTextInRun( LogicalDirection.Backward, bc, 0, 1 );
            int fn = start.GetTextInRun( LogicalDirection.Forward, fc, 0, 1 );

            s += $", Bc: '{( bn == 0 ? '∅' : bc[0] )}', Fc: '{( fn == 0 ? '∅' : fc[0] )}";

            lblDbgInfo.Content = s;
        }


        #region IDisposable Support

        private bool disposedValue = false; // To detect redundant calls

        protected virtual void Dispose( bool disposing )
        {
            if( !disposedValue )
            {
                if( disposing )
                {
                    // TODO: dispose managed state (managed objects).

                    using( RecolouringLoop ) { }
                    using( LocalUnderliningLoop ) { }
                    using( ExternalUnderliningLoop ) { }
                }

                // TODO: free unmanaged resources (unmanaged objects) and override a finalizer below.
                // TODO: set large fields to null.

                disposedValue = true;
            }
        }

        // TODO: override a finalizer only if Dispose(bool disposing) above has code to free unmanaged resources.
        // ~UCText()
        // {
        //   // Do not change this code. Put cleanup code in Dispose(bool disposing) above.
        //   Dispose(false);
        // }

        // This code added to correctly implement the disposable pattern.
        public void Dispose( )
        {
            // Do not change this code. Put cleanup code in Dispose(bool disposing) above.
            Dispose( true );
            // TODO: uncomment the following line if the finalizer is overridden above.
            // GC.SuppressFinalize(this);
        }

        #endregion IDisposable Support

    }
}
