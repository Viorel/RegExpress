using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Printing;
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
using RegExpressWPFNET.Code.OutputInfo;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for UCMatches.xaml
    /// </summary>
    partial class UCMatches : UserControl, IDisposable
    {
        readonly UnderliningAdorner LocalUnderliningAdorner;
        readonly UnderliningAdorner ExternalUnderliningAdorner;

        readonly ResumableLoop ShowMatchesLoop;
        readonly ResumableLoop LocalUnderliningLoop;
        readonly ResumableLoop ExternalUnderliningLoop;

        readonly ChangeEventHelper ChangeEventHelper;

        readonly StyleInfo[] HighlightStyleInfos;
        readonly StyleInfo[] HighlightLightStyleInfos;
        readonly StyleInfo MatchNormalStyleInfo;
        readonly StyleInfo MatchValueStyleInfo;
        readonly StyleInfo MatchValueSpecialStyleInfo;
        readonly StyleInfo LocationStyleInfo;
        readonly StyleInfo GroupNameStyleInfo;
        readonly StyleInfo GroupSiblingValueStyleInfo;
        readonly StyleInfo GroupValueStyleInfo;
        readonly StyleInfo GroupFailedStyleInfo;
        readonly StyleInfo GroupOverflowStyleInfo;

        bool AlreadyLoaded = false;

        const int MIN_LEFT_WIDTH = 18;

        string? LastText;
        RegexMatches LastMatches = RegexMatches.Empty;
        bool LastShowFirstOnly;
        bool LastShowSucceededGroupsOnly;
        bool LastShowCaptures;
        bool LastNoGroupDetails;
        IReadOnlyList<Segment>? LastExternalUnderliningSegments;
        bool LastExternalUnderliningSetSelection;

        readonly List<MatchInfo> MatchInfos = new( );
        int MatchInfosVersion = 0;

        public event EventHandler? SelectionChanged;
        public event EventHandler? Cancelled;


        public UCMatches( )
        {
            InitializeComponent( );

            LocalUnderliningAdorner = new UnderliningAdorner( rtbMatches );
            ExternalUnderliningAdorner = new UnderliningAdorner( rtbMatches );

            ChangeEventHelper = new ChangeEventHelper( rtbMatches );

            HighlightStyleInfos = new[]
            {
                new StyleInfo( "MatchHighlight_0" ),
                new StyleInfo( "MatchHighlight_1" ),
                new StyleInfo( "MatchHighlight_2" )
            };

            HighlightLightStyleInfos = new[]
            {
                new StyleInfo( "MatchHighlight_0_Light" ),
                new StyleInfo( "MatchHighlight_1_Light" ),
                new StyleInfo( "MatchHighlight_2_Light" )
            };

            MatchNormalStyleInfo = new StyleInfo( "MatchNormal" );
            MatchValueStyleInfo = new StyleInfo( "MatchValue" );
            MatchValueSpecialStyleInfo = new StyleInfo( "MatchValueSpecial" );
            LocationStyleInfo = new StyleInfo( "MatchLocation" );
            GroupNameStyleInfo = new StyleInfo( "MatchGroupName" );
            GroupSiblingValueStyleInfo = new StyleInfo( "MatchGroupSiblingValue" );
            GroupValueStyleInfo = new StyleInfo( "MatchGroupValue" );
            GroupFailedStyleInfo = new StyleInfo( "MatchGroupFailed" );
            GroupOverflowStyleInfo = new StyleInfo( "GroupOverflow" );

            secOverflow.Blocks.Remove( paraOverflow );

            ShowMatchesLoop = new ResumableLoop( ShowMatchesThreadProc, 333, 555 );
            LocalUnderliningLoop = new ResumableLoop( LocalUnderliningThreadProc, 222, 444 );
            ExternalUnderliningLoop = new ResumableLoop( ExternalUnderliningThreadProc, 333, 555 );


            pnlDebug.Visibility = Visibility.Collapsed;
#if !DEBUG
			pnlDebug.Visibility = Visibility.Collapsed;
#endif
            //LocalUnderliningAdorner.IsDbgDisabled = true; 
            //ExternalUnderliningAdorner.IsDbgDisabled = true;
        }


        public void Shutdown( )
        {
            StopAll( );
        }


        public void ShowInfo( string text, bool showCancelButton )
        {
            runInfo.Text = text;
            rtbInfo.ScrollToHome( );
            rtbInfo.Visibility = Visibility.Visible;
            btnCancel.Visibility = showCancelButton ? Visibility.Visible : Visibility.Collapsed;
        }


        private void CancelInfo( )
        {
            rtbInfo.Visibility = Visibility.Hidden;
        }


        public void ShowError( Exception exc, bool scrollToEnd )
        {
            StopAll( );

            lock( this )
            {
                LastText = null;
                LastMatches = RegexMatches.Empty;
                LastExternalUnderliningSegments = null;
            }

            Dispatcher.BeginInvoke( new Action( ( ) =>
            {
                CancelInfo( );
                ShowOne( rtbError );
                runError.Text = exc.Message.Trim( ) + Environment.NewLine;
                if( scrollToEnd )
                {
                    rtbError.ScrollToEnd( );
                }
                else
                {
                    rtbError.ScrollToHome( );
                }
            } ) );
        }


        public void ShowNoPattern( )
        {
            StopAll( );

            lock( this )
            {
                LastText = null;
                LastMatches = RegexMatches.Empty;
                LastExternalUnderliningSegments = null;
            }

            Dispatcher.BeginInvoke( new Action( ( ) =>
            {
                CancelInfo( );
                ShowOne( rtbNoPattern );
            } ) );
        }


        public void ShowIndeterminateProgress( bool yes )
        {
            pbProgressIndeterminate.Visibility = yes ? Visibility.Visible : Visibility.Hidden;
        }


        public void ShowMatchingInProgress( bool yes )
        {
#if DEBUG
            pnlHourglass.Visibility = yes ? Visibility.Visible : Visibility.Hidden;
#else
            pnlHourglass.Visibility = Visibility.Hidden;
#endif
        }


        public void SetMatches( string text, RegexMatches matches, bool showFirstOnly, bool showSucceededGroupsOnly, bool showCaptures, bool noGroupDetails )
        {
            if( matches == null ) throw new ArgumentNullException( nameof( matches ) );

            lock( this )
            {
                if( LastMatches != null )
                {
                    var old_groups = LastMatches.Matches.SelectMany( m => m.Groups ).Select( g => (g.Index, g.Length, g.Value, g.Name) );
                    var new_groups = matches.Matches.SelectMany( m => m.Groups ).Select( g => (g.Index, g.Length, g.Value, g.Name) );

                    var old_captures = LastMatches.Matches.SelectMany( m => m.Groups ).SelectMany( g => g.Captures ).Select( c => c.Value );
                    var new_captures = matches.Matches.SelectMany( m => m.Groups ).SelectMany( g => g.Captures ).Select( c => c.Value );

                    if( rtbMatches.IsVisible &&
                        showFirstOnly == LastShowFirstOnly &&
                        showSucceededGroupsOnly == LastShowSucceededGroupsOnly &&
                        showCaptures == LastShowCaptures &&
                        noGroupDetails == LastNoGroupDetails &&
                        new_groups.SequenceEqual( old_groups ) &&
                        new_captures.SequenceEqual( old_captures ) )
                    {
                        CancelInfo( );

                        LastText = text;
                        LastMatches = matches;
                        LastExternalUnderliningSegments = null;

                        return;
                    }
                }
            }

            ShowMatchesLoop.SignalRewind( );
            LocalUnderliningLoop.SignalRewind( );
            ExternalUnderliningLoop.SignalRewind( );
            LocalUnderliningAdorner.SetRangesToUnderline( null ); //?
            ExternalUnderliningAdorner.SetRangesToUnderline( null ); //?

            lock( this )
            {
                LastText = text;
                LastMatches = matches;
                LastShowCaptures = showCaptures;
                LastShowFirstOnly = showFirstOnly;
                LastShowSucceededGroupsOnly = showSucceededGroupsOnly;
                LastNoGroupDetails = noGroupDetails;
                LastExternalUnderliningSegments = null;
            }

            ShowMatchesLoop.SignalWaitAndExecute( );
            LocalUnderliningLoop.SignalWaitAndExecute( );
            ExternalUnderliningLoop.SignalWaitAndExecute( );
        }


        public void SetExternalUnderlining( IReadOnlyList<Segment>? segments, bool setSelection )
        {
            ExternalUnderliningLoop.SignalRewind( );

            lock( this )
            {
                LastExternalUnderliningSegments = segments;
                LastExternalUnderliningSetSelection = setSelection;
            }

            ExternalUnderliningLoop.SignalWaitAndExecute( );
        }


        public void EnableUnderlining( bool yes )
        {
            LocalUnderliningAdorner.IsOn = yes;
            ExternalUnderliningAdorner.IsOn = yes;

            LocalUnderliningLoop.SignalWaitAndExecute( );
            ExternalUnderliningLoop.SignalWaitAndExecute( );
        }


        public UnderlineInfo GetUnderlinedSegments( )
        {
            RegexMatches matches;

            lock( this )
            {
                matches = LastMatches;
            }

            List<Segment> segments = new( );

            if( !rtbMatches.IsFocused ||
                matches == null ||
                matches.Count == 0 )
            {
                return UnderlineInfo.Empty;
            }

            TextSelection sel = rtbMatches.Selection;

            for( var parent = sel.Start.Parent; parent != null; )
            {
                object? tag = null;

                switch( parent )
                {
                case FrameworkElement fe:
                    tag = fe.Tag;
                    parent = fe.Parent;
                    break;
                case FrameworkContentElement fce:
                    tag = fce.Tag;
                    parent = fce.Parent;
                    break;
                }

                switch( tag )
                {
                case MatchInfo mi:
                    segments.Add( mi.MatchSegment );
                    return new UnderlineInfo( segments );
                case GroupInfo gi:
                    if( !gi.NoGroupDetails ) if( gi.IsSuccess ) segments.Add( gi.GroupSegment );
                    return new UnderlineInfo( segments );
                case CaptureInfo ci:
                    segments.Add( ci.CaptureSegment );
                    return new UnderlineInfo( segments );
                }
            }

            return new UnderlineInfo( segments );
        }


        public void StopAll( )
        {
            ShowMatchesLoop.SignalRewind( );
            LocalUnderliningLoop.SignalRewind( );
            ExternalUnderliningLoop.SignalRewind( );
        }


        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( AlreadyLoaded ) return;

            rtbMatches.Document.PageWidth = double.NaN; // (NaN -- wrap)

            var adorner_layer = AdornerLayer.GetAdornerLayer( rtbMatches );
            adorner_layer.Add( LocalUnderliningAdorner );
            adorner_layer.Add( ExternalUnderliningAdorner );

            AlreadyLoaded = true;
        }


        private void BtnCancel_Click( object sender, RoutedEventArgs e )
        {
            Cancelled?.Invoke( this, EventArgs.Empty );
        }


        private void RtbMatches_SelectionChanged( object sender, RoutedEventArgs e )
        {
            if( !IsLoaded ) return;
            if( ChangeEventHelper.IsInChange ) return;
            if( !rtbMatches.IsFocused ) return;

            LocalUnderliningLoop.SignalWaitAndExecute( );

            SelectionChanged?.Invoke( this, EventArgs.Empty );

            ShowDebugInformation( ); // #if DEBUG
        }


        private void RtbMatches_GotFocus( object sender, RoutedEventArgs e )
        {
            LocalUnderliningLoop.SignalWaitAndExecute( );
            ExternalUnderliningLoop.SignalWaitAndExecute( );

            //...?SelectionChanged?.Invoke( this, null );

            if( Properties.Settings.Default.BringCaretIntoView )
            {
                if( rtbMatches.CaretPosition.Parent is FrameworkContentElement p )
                {
                    p.BringIntoView( );
                }
            }
        }


        private void RtbMatches_LostFocus( object sender, RoutedEventArgs e )
        {
            LocalUnderliningLoop.SignalWaitAndExecute( );
        }


        void ShowMatchesThreadProc( ICancellable cnc )
        {
            int match_infos_version = 0;

            lock( MatchInfos )
            {
                MatchInfos.Clear( );
                match_infos_version = ++MatchInfosVersion;
                ExternalUnderliningLoop.SignalWaitAndExecute( );
            }

            string text = "";
            RegexMatches matches;
            bool show_captures;
            bool show_succeeded_groups_only;
            bool show_first_only;
            bool no_group_details;

            lock( this )
            {
                text = LastText ?? "";
                matches = LastMatches;
                show_captures = LastShowCaptures;
                show_succeeded_groups_only = LastShowSucceededGroupsOnly;
                show_first_only = LastShowFirstOnly;
                no_group_details = LastNoGroupDetails;
            }


            if( matches.Count == 0 )
            {
                Dispatcher.BeginInvoke( new Action( ( ) =>
                {
                    CancelInfo( );
                    ShowOne( rtbNoMatches );
                } ) );

                return;
            }

            int matches_to_show = Math.Min( matches.Count, Properties.Settings.Default.MaxMatches < 0 ? int.MaxValue : Properties.Settings.Default.MaxMatches );
            Typeface? type_face = null;
            double font_size = 0;
            double pixels_per_dip = 0;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                pbProgress.Value = 0;
                pbProgress.Maximum = matches_to_show;

                if( secMatches.Blocks.Count > matches_to_show )
                {
                    // remove unneeded paragraphs
                    var r = new TextRange( secMatches.Blocks.ElementAt( matches_to_show ).ElementStart, secMatches.ContentEnd )
                    {
                        Text = ""
                    };
                }

                CancelInfo( );
                ShowOne( rtbMatches );

                if( matches_to_show != matches.Count )
                {
                    // later
                }
                else
                {
                    if( secOverflow.Blocks.Contains( paraOverflow ) )
                    {
                        secOverflow.Blocks.Remove( paraOverflow );
                    }
                }

                type_face = new Typeface( rtbMatches.FontFamily, rtbMatches.FontStyle, rtbMatches.FontWeight, rtbMatches.FontStretch );
                font_size = rtbMatches.FontSize;
                pixels_per_dip = VisualTreeHelper.GetDpi( rtbMatches ).PixelsPerDip;
            } );

            if( cnc.IsCancellationRequested ) return;

            int show_pb_time = unchecked(Environment.TickCount + 333); // (ignore overflow)

            Paragraph? previous_para = null;
            int match_number = -1;
            bool document_has_changed = false;
            double max_text_width = 0;

            int left_width = EvaluateLeftWidth( matches, show_succeeded_groups_only );

            foreach( IMatch match in matches.Matches )
            {
                if( cnc.IsCancellationRequested ) break;

                Debug.Assert( match.Success );

                ++match_number;

                var ordered_groups =
                                    match.Groups
                                        .Skip( 1 ) // skip main group (full match)
                                        .Where( g => g.Success || !show_succeeded_groups_only )
                                        //OrderBy( g => g.Success ? g.Index : match.Index )
                                        .ToList( );

                if( cnc.IsCancellationRequested ) break;


                int min_index = ordered_groups.Select( g => g.Success ? g.TextIndex : match.TextIndex ).Concat( new[] { match.TextIndex } ).Min( );
                if( show_captures )
                {
                    min_index = ordered_groups.SelectMany( g => g.Captures ).Select( c => c.TextIndex ).Concat( new[] { min_index } ).Min( );
                }

                if( cnc.IsCancellationRequested ) break;

                int left_width_for_match = left_width + ( match.TextIndex - min_index );

                Paragraph? para = null;
                Run? run = null;
                MatchInfo? match_info = null;
                OutputBuilder match_run_builder = new( MatchValueSpecialStyleInfo );
                bool max_number_achieved = false;

                var highlight_style = HighlightStyleInfos[match_number % HighlightStyleInfos.Length];
                var highlight_light_style = HighlightLightStyleInfos[match_number % HighlightStyleInfos.Length];

                // show match

                string match_name_text = show_first_only ? "Fɪʀꜱᴛ Mᴀᴛᴄʜ" : $"Mᴀᴛᴄʜ {match_number + 1}";
                string plain_text = "";

                ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                {
                    pbProgress.Value = match_number;
                    if( Environment.TickCount >= show_pb_time )
                    {
                        if( cnc.IsCancellationRequested ) return;
                        pbProgress.Visibility = Visibility.Visible;
                    }

                    if( match_number >= matches_to_show )
                    {
                        max_number_achieved = true;
                    }
                    else
                    {
                        Span span = new( );

                        para = new Paragraph( span );

                        string start_text = match_name_text.PadRight( left_width_for_match, ' ' );
                        var start_run = new Run( start_text, span.ContentEnd );
                        start_run.Style( MatchNormalStyleInfo );
                        plain_text += start_text;

                        Inline value_inline;

                        if( match.Length == 0 )
                        {
                            value_inline = new Run( "(empty)", span.ContentEnd ); //
                            value_inline.Style( MatchNormalStyleInfo, LocationStyleInfo );
                        }
                        else
                        {
                            (value_inline, string value_plain_text) = match_run_builder.Build( match.Value, span.ContentEnd );
                            value_inline.Style( MatchValueStyleInfo, highlight_style );
                            plain_text += value_plain_text;
                        }

                        string index_and_length = $"\x200E  （{match.Index}, {match.Length}）";
                        run = new Run( index_and_length, span.ContentEnd );
                        run.Style( MatchNormalStyleInfo, LocationStyleInfo );
                        plain_text += index_and_length;

                        _ = new LineBreak( span.ElementEnd ); // (after span)

                        match_info = new MatchInfo( matchSegment: new Segment( match.TextIndex, match.TextLength ), span: span, valueInline: value_inline );

                        span.Tag = match_info;

                        lock( MatchInfos )
                        {
                            MatchInfos.Add( match_info );

                            //...ExternalUnderliningEvents.SendRestart( );
                        }

                        // captures for match
                        //if( showCaptures) AppendCaptures( ct, para, LEFT_WIDTH, match, match );
                    }
                } );

                if( max_number_achieved ) break;
                if( cnc.IsCancellationRequested ) break;

                FormattedText ft = new(
                    plain_text, CultureInfo.CurrentCulture, FlowDirection.LeftToRight,
                    type_face,
                    font_size, Brushes.Black, pixels_per_dip );
                max_text_width = Math.Max( max_text_width, ft.Width );

                // show groups

                OutputBuilder sibling_run_builder = new( null );

                foreach( var group in ordered_groups )
                {
                    if( cnc.IsCancellationRequested ) break;

                    if( Properties.Settings.Default.MaxGroups >= 0 && match_info!.GroupInfos.Count >= Properties.Settings.Default.MaxGroups )
                    {
                        ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                        {
                            Run run = new( $"  ⚠ {ordered_groups.Count:#,##0} groups. The rest are not shown." );
                            run.Style( GroupOverflowStyleInfo );
                            para!.Inlines.Add( run );
                            _ = new LineBreak( run.ElementEnd );
                        } );

                        break;
                    }

                    string group_name_text = $" • Gʀᴏᴜᴘ ‹{group.Name}›";
                    int left_width_for_group = left_width_for_match - Math.Max( 0, match.TextIndex - ( group.Success ? group.TextIndex : match.TextIndex ) );

                    ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                    {
                        var span = new Span( );

                        var start_run = new Run( group_name_text.PadRight( left_width_for_group, ' ' ), span.ContentEnd );
                        start_run.Style( GroupNameStyleInfo );

                        // (NOTE. Overlaps of groups are possible in this example: (?=(..))

                        Inline value_inline;
                        Inline inl;

                        if( !group.Success )
                        {
                            value_inline = new Run( "(fail)", span.ContentEnd );
                            value_inline.Style( GroupFailedStyleInfo );
                        }
                        else if( group.Length == 0 )
                        {
                            value_inline = new Run( "(empty)", span.ContentEnd );
                            value_inline.Style( LocationStyleInfo );
                        }
                        else
                        {
                            string left;
                            string middle;
                            string right;

                            if( no_group_details )
                            {
                                left = "";
                                middle = group.Value;
                                right = "";
                            }
                            else
                            {
                                left = Utilities.SubstringFromTo( text, match.TextIndex, group.TextIndex );
                                middle = group.Value;
                                right = Utilities.SubstringFromTo( text, group.TextIndex + group.TextLength, Math.Max( match.TextIndex + match.TextLength, group.TextIndex + group.TextLength ) );
                            }

                            (inl, _) = sibling_run_builder.Build( left, span.ContentEnd );
                            inl.Style( GroupSiblingValueStyleInfo );

                            (value_inline, _) = match_run_builder.Build( middle, span.ContentEnd );
                            value_inline.Style( GroupValueStyleInfo, highlight_light_style );

                            (inl, _) = sibling_run_builder.Build( right, span.ContentEnd );
                            inl.Style( GroupSiblingValueStyleInfo );
                        }

                        if( cnc.IsCancellationRequested ) return;

                        if( group.Success )
                        {
                            if( !no_group_details )
                            {
                                run = new Run( $"\x200E  （{group.Index}, {group.Length}）", span.ContentEnd );
                                run.Style( MatchNormalStyleInfo, LocationStyleInfo );
                            }
                        }

                        para!.Inlines.Add( span );
                        _ = new LineBreak( span.ElementEnd ); // (after span)

                        var group_info = new GroupInfo( parent: match_info!, isSuccess: group.Success, groupSegment: new Segment( group.TextIndex, group.TextLength ), span: span, valueInline: value_inline, noGroupDetails: no_group_details );

                        span.Tag = group_info;

                        match_info!.GroupInfos.Add( group_info );


                        // captures for group
                        if( show_captures )
                        {
                            AppendCaptures( cnc, group_info, para, left_width_for_match, text, match, group, highlight_light_style, match_run_builder, sibling_run_builder );
                        }
                    } );
                }

                if( cnc.IsCancellationRequested ) break;

                ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                {
                    // adjust the horizontal scrollbar (increase only); will be also set at the end
                    if( max_text_width > 0 )
                    {
                        if( rtbMatches.Document.PageWidth == double.NaN || rtbMatches.Document.PageWidth < max_text_width ) rtbMatches.Document.PageWidth = max_text_width;
                    }

                    if( previous_para == null )
                    {
                        var first_block = secMatches.Blocks.FirstBlock;
                        if( first_block == null )
                        {
                            secMatches.Blocks.Add( para );
                        }
                        else
                        {
                            secMatches.Blocks.InsertBefore( first_block, para );
                            secMatches.Blocks.Remove( first_block );
                        }
                    }
                    else
                    {
                        if( !previous_para.ContentStart.IsInSameDocument( rtbMatches.Document.ContentStart ) )
                        {
                            document_has_changed = true;
                        }
                        else
                        {
                            var next = previous_para.NextBlock;
                            if( next != null ) secMatches.Blocks.Remove( next );

                            secMatches.Blocks.InsertAfter( previous_para, para );
                        }
                    }
                } );

                if( document_has_changed ) break;

                previous_para = para;
            } // (foreach match)

            if( document_has_changed ) return;

            if( cnc.IsCancellationRequested ) return;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                // adjust horizontal scrollbar
                if( rtbMatches.ViewportWidth > max_text_width )
                {
                    rtbMatches.Document.PageWidth = double.NaN;
                }
                else
                {
                    rtbMatches.Document.PageWidth = max_text_width;
                }

                pbProgress.Visibility = Visibility.Hidden;

                if( matches_to_show != matches.Count )
                {
                    if( !secOverflow.Blocks.Contains( paraOverflow ) )
                    {
                        secOverflow.Blocks.Add( paraOverflow );
                    }
                }
            } );


            ExternalUnderliningLoop.SignalWaitAndExecute( );

        }


        void AppendCaptures( ICancellable cnc, GroupInfo groupInfo, Paragraph para, int leftWidthForMatch,
            string text, IMatch match, IGroup group, StyleInfo highlightStyle,
            OutputBuilder runBuilder, OutputBuilder siblingRunBuilder )
        {
            int capture_number = -1;
            foreach( ICapture capture in group.Captures )
            {
                if( cnc.IsCancellationRequested ) break;

                if( Properties.Settings.Default.MaxCaptures >= 0 && groupInfo.CaptureInfos.Count >= Properties.Settings.Default.MaxCaptures )
                {
                    Run run = new( $"      ⚠ {group.Captures.Count( ):#,##0} captures. The rest are not shown." );
                    run.Style( GroupOverflowStyleInfo );
                    para.Inlines.Add( run );
                    _ = new LineBreak( run.ElementEnd );

                    break;
                }

                ++capture_number;

                var span = new Span( );

                string capture_name_text = $"  ◦ Cᴀᴘᴛᴜʀᴇ {capture_number}";
                int left_width_for_capture = leftWidthForMatch - Math.Max( 0, Math.Max( match.TextIndex - group.TextIndex, match.TextIndex - capture.TextIndex ) );

                var start_run = new Run( capture_name_text.PadRight( left_width_for_capture, ' ' ), span.ContentEnd );
                start_run.Style( GroupNameStyleInfo );

                Inline value_inline;
                Inline inline;

                if( capture.Length == 0 )
                {
                    value_inline = new Run( "(empty)", span.ContentEnd );
                    value_inline.Style( MatchNormalStyleInfo, LocationStyleInfo );
                }
                else
                {
                    string left = Utilities.SubstringFromTo( text, Math.Min( match.TextIndex, group.TextIndex ), capture.TextIndex );
                    string middle = capture.Value;
                    string right = Utilities.SubstringFromTo( text, capture.TextIndex + capture.TextLength, Math.Max( match.TextIndex + match.TextLength, group.TextIndex + group.TextLength ) );

                    (inline, _) = siblingRunBuilder.Build( left, span.ContentEnd );
                    inline.Style( GroupSiblingValueStyleInfo );

                    (value_inline, _) = runBuilder.Build( middle, span.ContentEnd );
                    value_inline.Style( GroupValueStyleInfo, highlightStyle );

                    (inline, _) = siblingRunBuilder.Build( right, span.ContentEnd );
                    inline.Style( GroupSiblingValueStyleInfo );
                }

                inline = new Run( $"\x200E  （{capture.Index}, {capture.Length}）", span.ContentEnd );
                inline.Style( MatchNormalStyleInfo, LocationStyleInfo );

                para.Inlines.Add( span );
                _ = new LineBreak( span.ElementEnd ); // (after span)

                var capture_info = new CaptureInfo( groupInfo, new Segment( capture.TextIndex, capture.TextLength ), span, value_inline );

                span.Tag = capture_info;

                groupInfo.CaptureInfos.Add( capture_info );
            }
        }


        List<Info> GetUnderliningInfos( ICancellable cnc )
        {
            List<Info> infos = new( );

            TextSelection sel = rtbMatches.Selection;

            for( var parent = sel.Start.Parent; parent != null; )
            {
                if( cnc.IsCancellationRequested ) return infos;

                object? tag = null;

                switch( parent )
                {
                case FrameworkElement fe:
                    tag = fe.Tag;
                    parent = fe.Parent;
                    break;
                case FrameworkContentElement fce:
                    tag = fce.Tag;
                    parent = fce.Parent;
                    break;
                }

                switch( tag )
                {
                case MatchInfo mi:
                    infos.Add( mi );
                    return infos;
                case GroupInfo gi:
                    if( !gi.NoGroupDetails ) infos.Add( gi );
                    return infos;
                case CaptureInfo ci:
                    infos.Add( ci );
                    return infos;
                }
            }

            return infos;
        }


        void LocalUnderliningThreadProc( ICancellable cnc )
        {
            List<Info>? infos = null;
            bool is_focused = true;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                infos = GetUnderliningInfos( cnc );
                is_focused = rtbMatches.IsFocused;
            } );

            if( cnc.IsCancellationRequested ) return;

            var inlines_to_underline = new List<Inline>( );

            if( is_focused )
            {
                foreach( var info in infos! )
                {
                    if( cnc.IsCancellationRequested ) break;

                    switch( info )
                    {
                    case MatchInfo mi:
                        inlines_to_underline.Add( mi.ValueInline );
                        break;
                    case GroupInfo gi:
                        if( gi.IsSuccess ) inlines_to_underline.Add( gi.ValueInline );
                        break;
                    case CaptureInfo ci:
                        inlines_to_underline.Add( ci.ValueInline );
                        break;
                    }
                }
            }

            if( cnc.IsCancellationRequested ) return;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
                        {
                            LocalUnderliningAdorner.SetRangesToUnderline(
                                inlines_to_underline
                                    .Select( i => (i.ContentStart, i.ContentEnd) )
                                    .ToList( ) );
                        } );
        }


        void ExternalUnderliningThreadProc( ICancellable cnc )
        {
            IReadOnlyList<Segment>? segments0;
            bool set_selection;
            bool no_group_details;

            lock( this )
            {
                segments0 = LastExternalUnderliningSegments;
                set_selection = LastExternalUnderliningSetSelection;
                no_group_details = LastNoGroupDetails;
            }

            var inlines_to_underline = new List<(Inline inline, Info info)>( );

            if( segments0 != null )
            {
                var segments = new HashSet<Segment>( segments0 );

                int match_infos_version;

                lock( MatchInfos )
                {
                    match_infos_version = MatchInfosVersion;
                }

                for( int i_m = 0; ; i_m++ )
                {
                    MatchInfo? mi = null;

                    lock( MatchInfos )
                    {
                        if( match_infos_version == MatchInfosVersion && i_m < MatchInfos.Count ) mi = MatchInfos[i_m];
                    }

                    if( mi == null ) break;

                    if( !no_group_details )
                    {
                        for( int i_g = 0; ; i_g++ )
                        {
                            if( cnc.IsCancellationRequested ) break;

                            GroupInfo? gi = null;

                            lock( MatchInfos )
                            {
                                if( match_infos_version == MatchInfosVersion && i_g < mi.GroupInfos.Count ) gi = mi.GroupInfos[i_g];
                            }

                            if( gi == null ) break;

                            if( segments.Contains( gi.GroupSegment ) )
                            {
                                inlines_to_underline.Add( (gi.ValueInline, gi) );
                            }

                            for( int i_c = 0; ; i_c++ )
                            {
                                if( cnc.IsCancellationRequested ) break;

                                CaptureInfo? ci = null;

                                lock( MatchInfos )
                                {
                                    if( match_infos_version == MatchInfosVersion && i_c < gi.CaptureInfos.Count ) ci = gi.CaptureInfos[i_c];
                                }

                                if( ci == null ) break;

                                if( segments.Contains( ci.CaptureSegment ) )
                                {
                                    inlines_to_underline.Add( (ci.ValueInline, ci) );
                                }
                            }
                        }
                    }

                    if( segments.Contains( mi.MatchSegment ) )
                    {
                        inlines_to_underline.Add( (mi.ValueInline, mi) );
                    }
                }

                if( cnc.IsCancellationRequested ) return;
            }

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                ExternalUnderliningAdorner.SetRangesToUnderline(
                    inlines_to_underline
                        .Select( r => (r.inline.ContentStart, r.inline.ContentEnd) )
                        .ToList( ) );

                /*
				 * Does not work well with another 'ScrollIntoView' that appears bellow
				var first_span = inlines_to_underline.FirstOrDefault( ).info?.GetMatchInfo( ).Span;

				if( first_span != null )
				{
					var rect = Rect.Union(
						first_span.ContentStart.GetCharacterRect( LogicalDirection.Forward ),
						first_span.ContentEnd.GetCharacterRect( LogicalDirection.Backward ) );

					RtbUtilities.ScrollIntoView( rtbMatches, rect, isRelativeRect: true );
				}
				*/
            } );

            if( cnc.IsCancellationRequested ) return;

            ChangeEventHelper.Invoke( CancellationToken.None, ( ) =>
            {
                var first_inline = inlines_to_underline.FirstOrDefault( ).inline;
                var last_inline = inlines_to_underline.LastOrDefault( ).inline;

                if( first_inline != null )
                {
                    Debug.Assert( last_inline != null );

                    RtbUtilities.BringIntoViewInvoked( cnc, rtbMatches,
                        first_inline.ContentStart, last_inline.ContentEnd, fullHorizontalScrollIfInvisible: true );
                }

                if( set_selection && !rtbMatches.IsKeyboardFocused )
                {
                    if( first_inline != null )
                    {
                        var p = first_inline.ContentStart.GetInsertionPosition( LogicalDirection.Forward );
                        rtbMatches.Selection.Select( p, p );
                    }
                }
            } );
        }


        void ShowOne( RichTextBox rtb )
        {
            void setVisibility( RichTextBox rtb1 )
            {
                var v = rtb1 == rtb ? Visibility.Visible : Visibility.Hidden;
                rtb1.Visibility = v;
            }

            setVisibility( rtbMatches );
            setVisibility( rtbNoMatches );
            setVisibility( rtbNoPattern );
            setVisibility( rtbError );

            if( !rtbMatches.IsVisible )
            {
                ChangeEventHelper.Do( ( ) =>
                {
                    secMatches.Blocks.Clear( );
                } );
            }

            if( !rtbError.IsVisible )
            {
                runError.Text = "";
            }

            pbProgress.Visibility = Visibility.Hidden;
            ShowIndeterminateProgress( false );
        }


        static int EvaluateLeftWidth( RegexMatches matches, bool showSucceededGroupsOnly )
        {
            if( matches == null ) return MIN_LEFT_WIDTH;

            int max_name_length = matches.Matches
                .SelectMany( m => m.Groups )
                .Where( g => !showSucceededGroupsOnly || g.Success )
                .Select( m => m.Name.Length )
                .Append( 0 )
                .Max( );

            int w = max_name_length + 11;

            if( w < MIN_LEFT_WIDTH ) return MIN_LEFT_WIDTH;

            return MIN_LEFT_WIDTH + ( ( w - MIN_LEFT_WIDTH ) / 4 + 1 ) * 4;
        }


        [Conditional( "DEBUG" )]
        private void ShowDebugInformation( )
        {
            string s = "";

            TextPointer start = rtbMatches.Selection.Start;

            Rect rectB = start.GetCharacterRect( LogicalDirection.Backward );
            Rect rectF = start.GetCharacterRect( LogicalDirection.Forward );

            s += $"BPos: {(int)rectB.Left}×{(int)rectB.Bottom}, FPos: {(int)rectF.Left}×{(int)rectF.Bottom}";

            char[] bc = new char[1];
            char[] fc = new char[1];

            int bn = start.GetTextInRun( LogicalDirection.Backward, bc, 0, 1 );
            int fn = start.GetTextInRun( LogicalDirection.Forward, fc, 0, 1 );

            s += $", Bc: '{( bn == 0 ? '∅' : bc[0] )}', Fc: '{( fn == 0 ? '∅' : fc[0] )}";

            lblDbgInfo.Content = s;
        }


        private void BtnDbgSave_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            rtbMatches.Focus( );

            Utilities.DbgSaveXAML( @"debug-ucmatches.xml", rtbMatches.Document );

            //SaveToPng( Window.GetWindow( this ), "debug-ucmatches.png" );
#endif
        }


        private void BtnDbgLoad_Click( object sender, RoutedEventArgs e )
        {
#if DEBUG
            rtbMatches.Focus( );

            Utilities.DbgLoadXAML( rtbMatches.Document, @"debug-ucmatches.xml" );
#endif
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

                    using( ShowMatchesLoop ) { }
                    using( LocalUnderliningLoop ) { }
                    using( ExternalUnderliningLoop ) { }
                }

                // TODO: free unmanaged resources (unmanaged objects) and override a finalizer below.
                // TODO: set large fields to null.

                disposedValue = true;
            }
        }

        // TODO: override a finalizer only if Dispose(bool disposing) above has code to free unmanaged resources.
        // ~UCMatches()
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
