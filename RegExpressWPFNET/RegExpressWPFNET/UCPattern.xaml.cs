using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
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
using RegExpressLibrary.SyntaxColouring;
using RegExpressWPFNET.Adorners;
using RegExpressWPFNET.Code;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for UCPattern.xaml
    /// </summary>
    partial class UCPattern : UserControl, IDisposable
    {
        readonly WhitespaceAdorner WhitespaceAdorner;

        readonly ResumableLoop RecolouringLoop;
        readonly ResumableLoop HighlightingLoop;

        readonly UndoRedoHelper UndoRedoHelper;

        bool AlreadyLoaded = false;

        readonly StyleInfo PatternNormalStyleInfo;
        readonly StyleInfo PatternGroupNameStyleInfo;
        readonly StyleInfo PatternCharacterClassStyleInfo;
        readonly StyleInfo PatternCharacterEscapeStyleInfo;
        readonly StyleInfo PatternEscapeStyleInfo;
        readonly StyleInfo PatternCommentStyleInfo;
        readonly StyleInfo PatternQuotedSequenceStyleInfo;
        readonly StyleInfo PatternAnchorStyleInfo;
        readonly StyleInfo PatternQuantifierStyleInfo;
        readonly StyleInfo PatternSymbolsStyleInfo;

        readonly StyleInfo PatternParaHighlightStyleInfo;
        readonly StyleInfo PatternCharClassBracketStyleInfo;
        readonly StyleInfo PatternCharClassBracketHighlightStyleInfo;
        readonly StyleInfo PatternRangeCurlyBraceHighlightStyleInfo;

        Segment LeftHighlightedParenthesis = Segment.Empty;
        Segment RightHighlightedParenthesis = Segment.Empty;
        Segment LeftHighlightedBracket = Segment.Empty;
        Segment RightHighlightedBracket = Segment.Empty;
        Segment LeftHighlightedCurlyBrace = Segment.Empty;
        Segment RightHighlightedCurlyBrace = Segment.Empty;

        IRegexEngine? mRegexEngine;
        string? mEol;

        public event EventHandler? TextChanged;
        public event EventHandler? SelectionChanged;


        public UCPattern( )
        {
            InitializeComponent( );

            UndoRedoHelper = new UndoRedoHelper( this.rtb );

            WhitespaceAdorner = new WhitespaceAdorner( rtb );

            PatternNormalStyleInfo = new StyleInfo( "PatternNormal" );
            PatternParaHighlightStyleInfo = new StyleInfo( "PatternParaHighlight" );
            PatternGroupNameStyleInfo = new StyleInfo( "PatternGroupName" );
            PatternCharacterClassStyleInfo = new StyleInfo( "PatternCharacterClass" );
            PatternCharacterEscapeStyleInfo = new StyleInfo( "PatternCharacterEscape" );
            PatternEscapeStyleInfo = new StyleInfo( "PatternEscape" );
            PatternQuotedSequenceStyleInfo = new StyleInfo( "PatternQuotedSequence" );
            PatternAnchorStyleInfo = new StyleInfo( "PatternAnchor" );
            PatternQuantifierStyleInfo = new StyleInfo( "PatternQuantifier" );
            PatternSymbolsStyleInfo = new StyleInfo( "PatternSymbol" );
            PatternCharClassBracketStyleInfo = new StyleInfo( "PatternCharacterClassBracket" );
            PatternCharClassBracketHighlightStyleInfo = new StyleInfo( "PatternCharacterClassBracketHighlight" );
            PatternRangeCurlyBraceHighlightStyleInfo = new StyleInfo( "PatternCurlyBraceHighlight" );
            PatternCommentStyleInfo = new StyleInfo( "PatternComment" );

            RecolouringLoop = new ResumableLoop( "Pattern Colour", RecolouringThreadProc, 222, 444 );
            HighlightingLoop = new ResumableLoop( "Pattern Highlight", HighlightingThreadProc, 111, 444 );

            //WhitespaceAdorner.IsDbgDisabled = true;
        }


        public void Shutdown( )
        {
            TerminateAll( );
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


        public void SetRegexOptions( IRegexEngine engine, string eol )
        {
            StopAll( );

            lock( this )
            {
                if( mRegexEngine != null )
                {
                    mRegexEngine.FeatureMatrixReady -= UCPattern_FeatureMatrixReady;
                }

                mRegexEngine = engine;
                mEol = eol;

                if( mRegexEngine != null )
                {
                    mRegexEngine.FeatureMatrixReady -= UCPattern_FeatureMatrixReady; //
                    mRegexEngine.FeatureMatrixReady += UCPattern_FeatureMatrixReady;
                }
            }

            if( IsLoaded )
            {
                RecolouringLoop.SignalWaitAndExecute( );
                HighlightingLoop.SignalWaitAndExecute( );
            }
        }


        private void UCPattern_FeatureMatrixReady( object? sender, EventArgs e )
        {
            if( object.ReferenceEquals( sender, mRegexEngine ) )
            {
                if( Dispatcher.CheckAccess( ) )
                {
                    RecolouringLoop.SignalWaitAndExecute( );
                }
                else
                {
                    Dispatcher.BeginInvoke( ( ) => RecolouringLoop.SignalWaitAndExecute( ) );
                }
            }
        }


        public void ShowWhiteSpaces( bool yes )
        {
            WhitespaceAdorner.ShowWhiteSpaces( yes );
        }


        public void SetFocus( )
        {
            rtb.Focus( );
            rtb.Selection.Select( rtb.Document.ContentStart, rtb.Document.ContentStart );
        }


        public void StopAll( )
        {
            RecolouringLoop.SignalRewind( );
            HighlightingLoop.SignalRewind( );
        }


        public void TerminateAll( )
        {
            RecolouringLoop.Terminate( );
            HighlightingLoop.Terminate( );
        }


        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( AlreadyLoaded ) return;

            // TODO: add an option
            //rtb.Document.MinPageWidth = Utilities.ToPoints( "21cm" );

            var adorner_layer = AdornerLayer.GetAdornerLayer( rtb );
            adorner_layer.Add( WhitespaceAdorner );

            AlreadyLoaded = true;
        }


        private void Rtb_SelectionChanged( object sender, RoutedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;
            if( !rtb.IsFocused ) return;

            UndoRedoHelper.HandleSelectionChanged( );
            HighlightingLoop.SignalWaitAndExecute( );

            SelectionChanged?.Invoke( this, EventArgs.Empty );
        }


        private void Rtb_TextChanged( object sender, TextChangedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;

            UndoRedoHelper.HandleTextChanged( e );

            LeftHighlightedParenthesis = Segment.Empty;
            RightHighlightedParenthesis = Segment.Empty;
            LeftHighlightedBracket = Segment.Empty;
            RightHighlightedBracket = Segment.Empty;
            LeftHighlightedCurlyBrace = Segment.Empty;
            RightHighlightedCurlyBrace = Segment.Empty;

            RecolouringLoop.SignalWaitAndExecute( );
            HighlightingLoop.SignalWaitAndExecute( );

            TextChanged?.Invoke( this, EventArgs.Empty );

            // apply default style to new text (not always complete; ignore errors)
            try
            {
                RtbUtilities.ApplyStyleToNewText( rtb, rtb.ChangeEventHelper, e.Changes, PatternNormalStyleInfo );
            }
            catch
            {
                // ignore
            }
        }


        private void Rtb_ScrollChanged( object sender, ScrollChangedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;

            RecolouringLoop.SignalWaitAndExecute( );
            HighlightingLoop.SignalWaitAndExecute( );
        }


        private void Rtb_SizeChanged( object sender, SizeChangedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;

            RecolouringLoop.SignalWaitAndExecute( );
            HighlightingLoop.SignalWaitAndExecute( );
        }


        private void Rtb_GotFocus( object sender, RoutedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;

            HighlightingLoop.SignalWaitAndExecute( );

            if( Properties.Settings.Default.BringCaretIntoView )
            {
                if( rtb.CaretPosition?.Parent is FrameworkContentElement p )
                {
                    p.BringIntoView( );
                }
            }
        }


        private void Rtb_LostFocus( object sender, RoutedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( rtb.ChangeEventHelper.IsInChange ) return;

            HighlightingLoop.SignalWaitAndExecute( );
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


        readonly Lock HighlighterLocker = new( );


        void RecolouringThreadProc( ICancellable cnc )
        {
            IRegexEngine? regex_engine;
            string? eol;

            lock( this )
            {
                regex_engine = mRegexEngine;
                eol = mEol;
            }

            if( regex_engine == null ) return;

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
            //Debug.Assert( bottom_index < td.OldPointers.Count );
            Debug.Assert( bottom_index <= td.Text.Length );

            var visible_segment = new Segment( top_index, bottom_index - top_index + 1 );
            var segments_to_colourise = new ColouredSegments( );

            SyntaxColourer.ColourisePattern( cnc, segments_to_colourise, td.Text, visible_segment, regex_engine.GetSyntaxOptions( ) );

            if( cnc.IsCancellationRequested ) return;

            foreach( var s in segments_to_colourise.GroupNames )
            {
                Segment.Except( segments_to_colourise.Symbols, s );
            }

            //int center_index = ( top_index + bottom_index ) / 2;
            // just for fun?
            //var arranged_escapes = segments_to_colourise.Escapes
            //    .OrderBy( s => Math.Abs( center_index - ( s.Index + s.Length / 2 ) ) )
            //    .ToList( );

#if false
            // unordered

            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Symbols, PatternSymbolsStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.GroupNames, PatternGroupNameStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.CharacterClass, PatternCharacterClassStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.CharacterEscapes, PatternCharacterEscapeStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Escapes, PatternEscapeStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Anchors, PatternAnchorStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Quantifiers, PatternQuantifierStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Brackets, PatternCharClassBracketStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.Comments, PatternCommentStyleInfo );
            RtbUtilities.ApplyStyle( cnc, ChangeEventHelper, null, td, segments_to_colourise.QuotedSequences, PatternQuotedSequenceStyleInfo );
#else
            // ordered

            //var ordered_segments =
            //segments_to_colourise.Comments.Select( s => (s, PatternCommentStyleInfo) )
            //    .Concat( segments_to_colourise.CharacterClass.Select( s => (s, PatternCharacterClassStyleInfo) ) )
            //    .Concat( segments_to_colourise.CharacterEscapes.Select( s => (s, PatternCharacterEscapeStyleInfo) ) )
            //    .Concat( segments_to_colourise.Escapes.Select( s => (s, PatternEscapeStyleInfo) ) )
            //    .Concat( segments_to_colourise.QuotedSequences.Select( s => (s, PatternQuotedSequenceStyleInfo) ) )
            //    .Concat( segments_to_colourise.Anchors.Select( s => (s, PatternAnchorStyleInfo) ) )
            //    .Concat( segments_to_colourise.Quantifiers.Select( s => (s, PatternQuantifierStyleInfo) ) )
            //    .Concat( segments_to_colourise.Symbols.Select( s => (s, PatternSymbolsStyleInfo) ) )
            //    .Concat( segments_to_colourise.Brackets.Select( s => (s, PatternCharClassBracketStyleInfo) ) )
            //    .Concat( segments_to_colourise.GroupNames.Select( s => (s, PatternGroupNameStyleInfo) ) )
            //    .OrderBy( p => p.s.Index )
            //    .ToList( );

            var ordered_segments = new List<(Segment, StyleInfo)>( );
            segments_to_colourise.Comments.ForEach( s => ordered_segments.Add( (s, PatternCommentStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.CharacterClass.ForEach( s => ordered_segments.Add( (s, PatternCharacterClassStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.CharacterEscapes.ForEach( s => ordered_segments.Add( (s, PatternCharacterEscapeStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.Escapes.ForEach( s => ordered_segments.Add( (s, PatternEscapeStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.QuotedSequences.ForEach( s => ordered_segments.Add( (s, PatternQuotedSequenceStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.Anchors.ForEach( s => ordered_segments.Add( (s, PatternAnchorStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.Quantifiers.ForEach( s => ordered_segments.Add( (s, PatternQuantifierStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.Symbols.ForEach( s => ordered_segments.Add( (s, PatternSymbolsStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.Brackets.ForEach( s => ordered_segments.Add( (s, PatternCharClassBracketStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;
            segments_to_colourise.GroupNames.ForEach( s => ordered_segments.Add( (s, PatternGroupNameStyleInfo) ) );
            if( cnc.IsCancellationRequested ) return;

            ordered_segments.Sort( ( p1, p2 ) => p1.Item1.Index.CompareTo( p2.Item1.Index ) );

            RtbUtilities.ApplyStyle( cnc, rtb.ChangeEventHelper, null, td, ordered_segments );
#endif

            var uncovered_segments = new List<Segment> { new Segment( 0, td.Text.Length ) };

            foreach( var s in segments_to_colourise.All.SelectMany( s => s ) )
            {
                if( cnc.IsCancellationRequested ) return;

                Segment.Except( uncovered_segments, s );
            }

            lock( HighlighterLocker )
            {
                Segment.Except( uncovered_segments, LeftHighlightedParenthesis );
                Segment.Except( uncovered_segments, RightHighlightedParenthesis );
                Segment.Except( uncovered_segments, LeftHighlightedBracket );
                if( cnc.IsCancellationRequested ) return;
                Segment.Except( uncovered_segments, RightHighlightedBracket );
                Segment.Except( uncovered_segments, LeftHighlightedCurlyBrace );
                Segment.Except( uncovered_segments, RightHighlightedCurlyBrace );
            }

            //
            //var segments_to_uncolour =
            //    uncovered_segments
            //        .Select( s => Segment.Intersection( s, visible_segment ) )
            //        .Where( s => !s.IsEmpty )
            //        .OrderBy( s => Math.Abs( center_index - ( s.Index + s.Length / 2 ) ) )
            //        .ToList( );
            var segments_to_uncolour =
                uncovered_segments
                    .Select( s => Segment.Intersection( s, visible_segment ) )
                    .Where( s => !s.IsEmpty )
                    .ToList( );

            if( cnc.IsCancellationRequested ) return;

            RtbUtilities.ApplyStyle( cnc, rtb.ChangeEventHelper, null, td, segments_to_uncolour, PatternNormalStyleInfo );
        }


        void HighlightingThreadProc( ICancellable cnc )
        {
            IRegexEngine? regex_engine;
            string? eol;

            lock( this )
            {
                regex_engine = mRegexEngine;
                eol = mEol;
            }

            if( regex_engine == null ) return;

            TextData? td = null;
            Rect clip_rect = Rect.Empty;
            int top_index = 0;
            int bottom_index = 0;
            bool is_focused = false;

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

                is_focused = rtb.IsFocused;
            } );

            if( cnc.IsCancellationRequested ) return;

            if( td == null ) return;
            if( td.Text.Length == 0 ) return;

            Debug.Assert( top_index >= 0 );
            Debug.Assert( bottom_index >= top_index );
            Debug.Assert( bottom_index <= td.Text.Length );

            Highlights? highlights = null;

            if( is_focused )
            {
                var visible_segment = new Segment( top_index, bottom_index - top_index + 1 );
                highlights = new Highlights( );

                SyntaxColourer.HighlightPattern( cnc, highlights, td.Text, td.Selection.Start, td.Selection.End, visible_segment, regex_engine.GetSyntaxOptions( ) );
            }

            if( cnc.IsCancellationRequested ) return;

            rtb.ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                lock( HighlighterLocker )
                {
                    TryHighlight( ref LeftHighlightedParenthesis, highlights?.LeftPar ?? Segment.Empty, td, PatternParaHighlightStyleInfo, PatternSymbolsStyleInfo );
                    if( cnc.IsCancellationRequested ) return;

                    TryHighlight( ref RightHighlightedParenthesis, highlights?.RightPar ?? Segment.Empty, td, PatternParaHighlightStyleInfo, PatternSymbolsStyleInfo );
                    if( cnc.IsCancellationRequested ) return;

                    TryHighlight( ref LeftHighlightedBracket, highlights?.LeftBracket ?? Segment.Empty, td, PatternCharClassBracketHighlightStyleInfo, PatternCharClassBracketStyleInfo );
                    if( cnc.IsCancellationRequested ) return;

                    TryHighlight( ref RightHighlightedBracket, highlights?.RightBracket ?? Segment.Empty, td, PatternCharClassBracketHighlightStyleInfo, PatternCharClassBracketStyleInfo );
                    if( cnc.IsCancellationRequested ) return;

                    TryHighlight( ref LeftHighlightedCurlyBrace, highlights?.LeftCurlyBrace ?? Segment.Empty, td, PatternRangeCurlyBraceHighlightStyleInfo, PatternQuantifierStyleInfo );
                    if( cnc.IsCancellationRequested ) return;

                    TryHighlight( ref RightHighlightedCurlyBrace, highlights?.RightCurlyBrace ?? Segment.Empty, td, PatternRangeCurlyBraceHighlightStyleInfo, PatternQuantifierStyleInfo );
                    if( cnc.IsCancellationRequested ) return;
                }
            } );
        }

        static void TryHighlight( ref Segment currentSegment, Segment newSegment, TextData td, StyleInfo styleInfo, StyleInfo unhighlightStyleInfo )
        {
            // TODO: avoid flickering

            if( !currentSegment.IsEmpty && currentSegment != newSegment )
            {
                var tr = td.Range( currentSegment );
                tr.Style( unhighlightStyleInfo );
            }

            currentSegment = newSegment;

            if( !currentSegment.IsEmpty )
            {
                var tr = td.RangeFB( currentSegment.Index, currentSegment.Length );
                tr.Style( styleInfo );
            }
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
                    using( HighlightingLoop ) { }
                }

                // TODO: free unmanaged resources (unmanaged objects) and override a finalizer below.
                // TODO: set large fields to null.

                disposedValue = true;
            }
        }

        // TODO: override a finalizer only if Dispose(bool disposing) above has code to free unmanaged resources.
        // ~UCPattern()
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
