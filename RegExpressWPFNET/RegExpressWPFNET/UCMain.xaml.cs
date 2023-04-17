using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Xml;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.SyntaxColouring;
using RegExpressWPFNET.Code;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for UCMain.xaml
    /// </summary>
    partial class UCMain : UserControl, IDisposable
    {
        readonly ResumableLoop FindMatchesLoop;
        readonly ResumableLoop UpdateWhitespaceWarningLoop;
        readonly ResumableLoop ShowTextInfoLoop;

        readonly Regex RegexHasWhitespace = new Regex( "\t|([ ](\r|\n|$))|((\r|\n)$)", RegexOptions.Compiled | RegexOptions.ExplicitCapture );


        readonly IReadOnlyList<IRegexEngine> RegexEngines;
        readonly IRegexEngine DefaultRegexEngine;
        IRegexEngine? CurrentRegexEngine = null;
        readonly HashSet<IRegexEngine> RegexEnginesUsed = new( );

        bool IsFullyLoaded = false;
        bool IsInChange = false;
        TabData? InitialTabData = null;
        bool ucTextHadFocus = false;

        public event EventHandler? Changed;
        public event EventHandler? NewTabClicked;


        public UCMain( IReadOnlyList<IRegexEngine> engines )
        {
            InitializeComponent( );

            RegexEngines = engines;
            DefaultRegexEngine = engines.First( );

            lblTextInfo.Visibility = Visibility.Collapsed;
            lblWarnings.Inlines.Remove( lblWhitespaceWarning1 );
            lblWarnings.Inlines.Remove( lblWhitespaceWarning2 );
            pnlOwerlappingMatches.Visibility = Visibility.Collapsed;

            FindMatchesLoop = new ResumableLoop( FindMatchesThreadProc, 333, 555 );
            UpdateWhitespaceWarningLoop = new ResumableLoop( UpdateWhitespaceWarningThreadProc, 444, 777 );
            ShowTextInfoLoop = new ResumableLoop( ShowTextInfoThreadProc, 333, 555 );

            UpdateWhitespaceWarningLoop.Priority = ThreadPriority.Lowest;
            ShowTextInfoLoop.Priority = ThreadPriority.Lowest;

            foreach( var eng in RegexEngines )
            {
                eng.OptionsChanged += Engine_OptionsChanged;

                var cbxi = new ComboBoxItem
                {
                    Tag = eng.CombinedId,
                    Content = eng.Name + " " + ( eng.Version?.ToString( ) ?? "unknown version" ),
                    IsSelected = eng.CombinedId == DefaultRegexEngine.CombinedId,
                };

                cbxEngine.Items.Add( cbxi );
            }

            // because 'Unloaded' event is not always called
            // (https://stackoverflow.com/questions/14479038/how-to-fire-unload-event-of-usercontrol-in-a-wpf-window)

            Dispatcher.ShutdownStarted += HandleShutdownStarted;
        }


        private void HandleShutdownStarted( object? sender, EventArgs e )
        {
            Shutdown( );
        }


        public void Shutdown( )
        {
            TerminateAll( );

            ucPattern.Shutdown( );
            ucText.Shutdown( );
            ucMatches.Shutdown( );
        }


        public void ApplyTabData( TabData tabData )
        {
            Debug.Assert( !IsInChange );

            if( !IsFullyLoaded || !IsVisible )
            {
                InitialTabData = tabData;
            }
            else
            {
                InitialTabData = null;

                StopAll( );
                LoadTabData( tabData );
                RestartAll( );
            }
        }


        public void ExportTabData( TabData tabData )
        {
            if( InitialTabData != null )
            {
                // did not have chance to finish initialisation 

                tabData.Pattern = InitialTabData.Pattern;
                tabData.Text = InitialTabData.Text;
                tabData.ActiveKind = InitialTabData.ActiveKind;
                tabData.ActiveVersion = InitialTabData.ActiveVersion;
                tabData.EngineOptions = InitialTabData.EngineOptions ?? new( );
                tabData.ShowFirstMatchOnly = InitialTabData.ShowFirstMatchOnly;
                tabData.ShowSucceededGroupsOnly = InitialTabData.ShowSucceededGroupsOnly;
                tabData.ShowCaptures = InitialTabData.ShowCaptures;
                tabData.ShowWhiteSpaces = InitialTabData.ShowWhiteSpaces;
                tabData.Eol = InitialTabData.Eol;
                tabData.Metrics = InitialTabData.Metrics;
            }
            else
            {
                tabData.Pattern = ucPattern.GetBaseTextData( "\n" ).Text;
                tabData.Text = ucText.GetBaseTextData( "\n" ).Text;
                tabData.ActiveKind = CurrentRegexEngine!.Kind;
                tabData.ActiveVersion = CurrentRegexEngine.Version;
                tabData.ShowFirstMatchOnly = cbShowFirstOnly.IsChecked == true;
                tabData.ShowSucceededGroupsOnly = cbShowSucceededGroupsOnly.IsChecked == true;
                tabData.ShowCaptures = cbShowCaptures.IsChecked == true;
                tabData.ShowWhiteSpaces = cbShowWhitespaces.IsChecked == true;
                tabData.Eol = GetEolOption( );
                tabData.Metrics = GetMetrics( );

                // save options of active and inactive engines

                tabData.EngineOptions = new( );

                foreach( var engine in RegexEngines )
                {
                    if( RegexEnginesUsed.Contains( engine ) )
                    {
                        tabData.EngineOptions.Add( new EngineOptions { Kind = engine.Kind, Version = engine.Version, Options = engine.ExportOptions( ) } );
                    }
                }
            }
        }


        public TabMetrics GetMetrics( )
        {
            var metrics = new TabMetrics
            {
                RightColumnWidth = RightColumn.ActualWidth,
                TopRowHeight = TopRow.ActualHeight,
                BottomRowHeight = BottomRow.ActualHeight
            };

            return metrics;
        }


        public void ApplyMetrics( TabMetrics metrics, bool full )
        {
            if( metrics.RightColumnWidth > 20 ) RightColumn.Width = new GridLength( metrics.RightColumnWidth );

            if( full )
            {
                if( metrics.TopRowHeight > 20 ) TopRow.Height = new GridLength( metrics.TopRowHeight );
                if( metrics.BottomRowHeight > 20 ) BottomRow.Height = new GridLength( metrics.BottomRowHeight );
            }

            if( InitialTabData != null )
            {
                InitialTabData.Metrics = metrics;
            }
        }


        public void GoToOptions( )
        {
            cbxEngine.Focus( );
        }


        private void UserControl_Loaded( object sender, RoutedEventArgs e )
        {
            if( IsFullyLoaded ) return;

            CurrentRegexEngine = RegexEngines.First( ); // default
            SetEngineOption( CurrentRegexEngine );

            ucPattern.SetFocus( );

            IsFullyLoaded = true;

            Debug.Assert( !IsInChange );

            if( IsVisible )
            {
                if( InitialTabData != null )
                {
                    var tab_data = InitialTabData;
                    InitialTabData = null;

                    StopAll( );
                    LoadTabData( tab_data );
                    RestartAll( );
                }
                else
                {
                    ucPattern.SetRegexOptions( CurrentRegexEngine, GetEolOption( ) );

                    StopAll( );
                    RestartAll( );
                }
            }
        }


        private void UserControl_Unloaded( object sender, RoutedEventArgs e )
        {
            //TerminateAll( );
        }


        private void UserControl_IsVisibleChanged( object sender, DependencyPropertyChangedEventArgs e )
        {
            Debug.Assert( !IsInChange );

            if( true.Equals( e.NewValue ) && IsFullyLoaded )
            {
                if( InitialTabData != null )
                {
                    StopAll( );

                    var tab_data = InitialTabData;
                    InitialTabData = null;

                    LoadTabData( tab_data );

                    RestartAll( );
                }
            }
        }


        private void BtnNewTab_Click( object sender, EventArgs e )
        {
            NewTabClicked?.Invoke( this, EventArgs.Empty );
        }


        private void UcPattern_TextChanged( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            FindMatchesLoop.SignalWaitAndExecute( );
            UpdateWhitespaceWarningLoop.SignalWaitAndExecute( );

            Changed?.Invoke( this, EventArgs.Empty );
        }


        private void UcText_TextChanged( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            FindMatchesLoop.SignalWaitAndExecute( );
            ShowTextInfoLoop.SignalWaitAndExecute( );
            UpdateWhitespaceWarningLoop.SignalWaitAndExecute( );

            Changed?.Invoke( this, EventArgs.Empty );
        }


        private void UcText_SelectionChanged( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ShowTextInfoLoop.SignalWaitAndExecute( );
        }


        private void ucText_GotKeyboardFocus( object sender, KeyboardFocusChangedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            if( !ucTextHadFocus )
            {
                ucTextHadFocus = true;

                ShowTextInfoLoop.SignalWaitAndExecute( );
            }
        }


        private void UcText_LostFocus( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ucMatches.SetExternalUnderlining( null, setSelection: false );
        }


        private void UcText_LocalUnderliningFinished( object sender, EventArgs e )
        {
            if( ucText.IsKeyboardFocusWithin )
            {
                var underlining_info = ucText.GetUnderliningInfo( );
                ucMatches.SetExternalUnderlining( underlining_info, setSelection: Properties.Settings.Default.MoveCaretToUnderlinedText );
            }
            else
            {
                ucMatches.SetExternalUnderlining( null, setSelection: false );
            }
        }


        private void UcMatches_SelectionChanged( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            var segments = ucMatches.GetUnderlinedSegments( );

            ucText.SetExternalUnderlining( segments, setSelection: Properties.Settings.Default.MoveCaretToUnderlinedText );
        }


        private void ucMatches_Cancelled( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            FindMatchesLoop.SignalRewind( );

            ucMatches.ShowError( new Exception( "Operation cancelled." ), false );
        }


        private void UcMatches_LostFocus( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ucText.SetExternalUnderlining( UnderlineInfo.Empty, setSelection: false );
        }


        private void cbxEngine_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            CurrentRegexEngine = RegexEngines.Single( eng => eng.CombinedId.Equals( ( (ComboBoxItem)e.AddedItems[0]! ).Tag ) );

            ShowOverlappingMatchesWarning( false );

            UpdateOptions( CurrentRegexEngine );

            HandleOptionsChange( preferImmediateReaction: true );
        }


        private void Engine_OptionsChanged( IRegexEngine sender, RegexEngineOptionsChangedArgs args )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            if( object.ReferenceEquals( sender, CurrentRegexEngine ) )
            {
                HandleOptionsChange( preferImmediateReaction: args?.PreferImmediateReaction == true );
                RegexEnginesUsed.Add( CurrentRegexEngine );
            }
            else
            {
                // inactive engine; ignore
            }
        }


        private void CbOption_CheckedChanged( object sender, RoutedEventArgs e )
        {
            HandleOptionsChange( preferImmediateReaction: false );
        }


        void HandleOptionsChange( bool preferImmediateReaction )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ucPattern.SetRegexOptions( CurrentRegexEngine!, GetEolOption( ) );

            if( preferImmediateReaction )
            {
                ucMatches.ShowInfo( "Matching…", false );
                lblMatches.Text = "Matches";

                FindMatchesLoop.SignalExecute( );
            }
            else
            {
                FindMatchesLoop.SignalWaitAndExecute( );
            }

            Changed?.Invoke( this, EventArgs.Empty );
        }


        private void CbShowWhitespaces_CheckedChanged( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ucPattern.ShowWhiteSpaces( cbShowWhitespaces.IsChecked == true );
            ucText.ShowWhiteSpaces( cbShowWhitespaces.IsChecked == true );

            UpdateWhitespaceWarningLoop.SignalWaitAndExecute( );

            Changed?.Invoke( this, EventArgs.Empty );
        }


        private void LnkShowAll_Click( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            cbShowFirstOnly.IsChecked = false;
        }


        private void LnkShowFirst_Click( object sender, RoutedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            cbShowFirstOnly.IsChecked = true;
        }


        private void CbxEol_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            if( !IsFullyLoaded ) return;
            if( IsInChange ) return;

            ucPattern.SetRegexOptions( CurrentRegexEngine!, GetEolOption( ) );
            FindMatchesLoop.SignalWaitAndExecute( );
            ShowTextInfoLoop.SignalWaitAndExecute( );

            Changed?.Invoke( this, EventArgs.Empty );
        }


        // --------------------


        private void LoadTabData( TabData tabData )
        {
            Debug.Assert( !IsInChange );
            IsInChange = true;

            try
            {
                // TODO: if no exact match, identify the most appropriate engine

                IRegexEngine? engine = RegexEngines.SingleOrDefault( eng => eng.CombinedId == tabData.ActiveCombinedId );
                if( engine == null ) engine = DefaultRegexEngine;

                CurrentRegexEngine = engine;
                SetEngineOption( engine );

                // set options of active and inactive engines
                foreach( var eng in RegexEngines )
                {
                    string? options = null;

                    // TODO: if no exact match, identify the most appropriate engine

                    options = tabData.EngineOptions?.FirstOrDefault( o => o.CombinedId == eng.CombinedId )?.Options;

                    if( options != null )
                    {
                        eng.ImportOptions( options );
                        RegexEnginesUsed.Add( eng );
                    }
                }

                cbShowFirstOnly.IsChecked = tabData.ShowFirstMatchOnly;
                cbShowSucceededGroupsOnly.IsChecked = tabData.ShowSucceededGroupsOnly;
                cbShowCaptures.IsChecked = tabData.ShowCaptures;
                cbShowWhitespaces.IsChecked = tabData.ShowWhiteSpaces;

                string eol = tabData.Eol ?? "\r\n";

                foreach( var item in cbxEol.Items.Cast<ComboBoxItem>( ) )
                {
                    item.IsSelected = (string)item.Tag == tabData.Eol;
                }
                if( cbxEol.SelectedItem == null ) ( (ComboBoxItem)cbxEol.Items[0] ).IsSelected = true;

                ucPattern.ShowWhiteSpaces( tabData.ShowWhiteSpaces );
                ucText.ShowWhiteSpaces( tabData.ShowWhiteSpaces );

                ucPattern.SetFocus( );

                ucPattern.SetRegexOptions( CurrentRegexEngine, eol );
                ucPattern.SetText( tabData.Pattern );
                ucText.SetText( tabData.Text );

                ApplyMetrics( tabData.Metrics, full: true );
            }
            finally
            {
                IsInChange = false;
            }
        }


        string GetEolOption( )
        {
            var eol_item = (ComboBoxItem)cbxEol.SelectedItem;
            string eol = (string)eol_item.Tag;

            return eol;
        }


        private void RestartAll( )
        {
            FindMatchesLoop.SignalWaitAndExecute( );
            ShowTextInfoLoop.SignalWaitAndExecute( );
            UpdateWhitespaceWarningLoop.SignalWaitAndExecute( );
        }


        private void TerminateAll( )
        {
            FindMatchesLoop.Terminate( );
            UpdateWhitespaceWarningLoop.Terminate( );
            ShowTextInfoLoop.Terminate( );
        }


        private void StopAll( )
        {
            FindMatchesLoop.SignalRewind( );
            UpdateWhitespaceWarningLoop.SignalRewind( );
            ShowTextInfoLoop.SignalRewind( );
        }


        void FindMatchesThreadProc( ICancellable cnc )
        {
            string eol = "\r\n";
            string pattern = "";
            string text = "";
            bool first_only = false;
            IRegexEngine? engine = null;

            UITaskHelper.Invoke( this,
                ( ) =>
                {
                    eol = GetEolOption( );
                    pattern = ucPattern.GetBaseTextData( eol ).Text;
                    if( cnc.IsCancellationRequested ) return;
                    text = ucText.GetBaseTextData( eol ).Text;
                    if( cnc.IsCancellationRequested ) return;
                    first_only = cbShowFirstOnly.IsChecked == true;
                    engine = CurrentRegexEngine;
                } );

            if( cnc.IsCancellationRequested ) return;

            if( string.IsNullOrEmpty( pattern ) )
            {
                UITaskHelper.BeginInvoke( this,
                    ( ) =>
                    {
                        ucText.SetMatches( RegexMatches.Empty, showCaptures: cbShowCaptures.IsChecked == true, eol: GetEolOption( ), potentialOverlaps: engine!.Capabilities.HasFlag( RegexEngineCapabilityEnum.OverlappingMatches ) );
                        ucMatches.ShowNoPattern( );
                        lblMatches.Text = "Matches";
                        ShowOverlappingMatchesWarning( false );
                    } );
            }
            else
            {
                IMatcher? parsed_pattern = null;
                RegexMatches matches = RegexMatches.Empty;
                bool is_good = false;

                try
                {
                    UITaskHelper.BeginInvoke( this, CancellationToken.None, ( ) => ucMatches.ShowMatchingInProgress( true ) );

                    parsed_pattern = engine!.ParsePattern( pattern );
                    var indeterminate_progress_thread = new Thread( IndeterminateProgressThreadProc ) { Name = "rxIndeterminate", IsBackground = true };
                    try
                    {
                        indeterminate_progress_thread.Start( );

                        matches = parsed_pattern.Matches( text ?? "", cnc );
                    }
                    finally
                    {
                        HideIndeterminateProgress( indeterminate_progress_thread );
                    }

                    is_good = true;
                }
                catch( Exception exc )
                {
                    if( cnc.IsCancellationRequested ) return;

                    UITaskHelper.BeginInvoke( this, CancellationToken.None,
                        ( ) =>
                        {
                            ucText.SetMatches( RegexMatches.Empty, showCaptures: cbShowCaptures.IsChecked == true, eol: GetEolOption( ), potentialOverlaps: engine!.Capabilities.HasFlag( RegexEngineCapabilityEnum.OverlappingMatches ) );
                            ucMatches.ShowError( exc, engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.ScrollErrorsToEnd ) );
                            lblMatches.Text = "Error";
                            ShowOverlappingMatchesWarning( false );
                        } );

                    Debug.Assert( !is_good );
                }
                finally
                {
                    UITaskHelper.BeginInvoke( this, CancellationToken.None, ( ) => ucMatches.ShowMatchingInProgress( false ) );
                }

                if( cnc.IsCancellationRequested ) return;

                if( is_good )
                {
                    int count = matches.Count;

                    var matches_to_show = first_only ?
                        new RegexMatches( Math.Min( 1, count ), matches.Matches.Take( 1 ) ) :
                        matches;

                    if( cnc.IsCancellationRequested ) return;

                    bool is_overlapping = false;

                    if( engine!.Capabilities.HasFlag( RegexEngineCapabilityEnum.OverlappingMatches ) )
                    {
                        var segments = new List<Segment>( );

                        foreach( var m in matches_to_show.Matches )
                        {
                            Debug.Assert( m.Success );

                            if( cnc.IsCancellationRequested ) return;

                            var new_seg = new Segment( m.Index, m.Length );

                            if( segments.Any( s => s.Intersects( new_seg ) ) ) // TODO: make cancellable?
                            {
                                is_overlapping = true;
                                break;
                            }

                            segments.Add( new_seg );
                        }
                    }

                    UITaskHelper.BeginInvoke( this,
                                    ( ) =>
                                    {
                                        ucText.SetMatches( matches_to_show, showCaptures: cbShowCaptures.IsChecked == true, eol: GetEolOption( ), potentialOverlaps: engine.Capabilities.HasFlag( RegexEngineCapabilityEnum.OverlappingMatches ) );
                                        ucMatches.SetMatches( text, matches_to_show, first_only, cbShowSucceededGroupsOnly.IsChecked == true, cbShowCaptures.IsChecked == true );

                                        lblMatches.Text = count == 0 ? "Matches" : count == 1 ? "1 match" : $"{count:#,##0} matches";
                                        ShowOverlappingMatchesWarning( is_overlapping );
                                    } );
                }
            }
        }


        void IndeterminateProgressThreadProc( )
        {
            try
            {
                Thread.Sleep( 2222 );

                UITaskHelper.Invoke( this, CancellationToken.None,
                        ( ) =>
                        {
                            ucMatches.ShowIndeterminateProgress( true );
                            ucMatches.ShowInfo( "The engine is busy, please wait…", true );
                            var engine = CurrentRegexEngine;
                            ucText.SetMatches( RegexMatches.Empty, showCaptures: cbShowCaptures.IsChecked == true, eol: GetEolOption( ), potentialOverlaps: engine!.Capabilities.HasFlag( RegexEngineCapabilityEnum.OverlappingMatches ) );
                        } );
            }
            catch( ThreadInterruptedException )
            {
                // ignore					   
            }
            catch( ThreadAbortException )
            {
                // ignore
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }
        }


        void HideIndeterminateProgress( Thread indeterminateProgressThread )
        {
            try
            {
                indeterminateProgressThread.Interrupt( );
                indeterminateProgressThread.Join( 333 );
                //NOT SUPPORTED: indeterminateProgressThread.Abort( );
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }

            UITaskHelper.BeginInvoke( this, CancellationToken.None,
            ( ) =>
            {
                ucMatches.ShowIndeterminateProgress( false );
            } );
        }


        void ShowTextInfoThreadProc( ICancellable cnc )
        {
            UITaskHelper.BeginInvoke( this,
                ( ) =>
                {
                    var td = ucText.GetTextData( GetEolOption( ) );

                    if( cnc.IsCancellationRequested ) return;

                    lblTextInfo.Visibility = lblTextInfo.Visibility == Visibility.Visible || td.Text.Length != 0 ? Visibility.Visible : Visibility.Collapsed;
                    if( lblTextInfo.Visibility == Visibility.Visible )
                    {
                        int text_elements = td.LengthInTextElements;

                        string s = $"(Length: {td.Text.Length:#,##0}";

                        if( text_elements != td.Text.Length )
                        {
                            s += $", Text Elements: {text_elements:#,##0}";
                        }

                        if( ucTextHadFocus )
                        {
                            s += $", Index: {td.SelectionStart:#,##0}";
                        }

                        s += ")";

                        lblTextInfo.Text = s;
                    }
                } );
        }


        void UpdateWhitespaceWarningThreadProc( ICancellable cnc )
        {
            bool has_whitespaces = false;
            bool show_whitespaces_option = false;
            string eol = "\r\n";
            BaseTextData? td = null;

            UITaskHelper.Invoke( this,
                ( ) =>
                {
                    show_whitespaces_option = cbShowWhitespaces.IsChecked == true;
                    eol = GetEolOption( );
                    td = ucPattern.GetBaseTextData( eol );

                    if( cnc.IsCancellationRequested ) return;
                } );

            if( cnc.IsCancellationRequested ) return;

            has_whitespaces = RegexHasWhitespace.IsMatch( td!.Text );

            if( !has_whitespaces )
            {
                UITaskHelper.Invoke( this,
                    ( ) =>
                    {
                        td = ucText.GetBaseTextData( eol );
                    } );

                has_whitespaces = RegexHasWhitespace.IsMatch( td.Text );
            }

            bool show1 = false;
            bool show2 = false;

            if( show_whitespaces_option )
            {
                if( has_whitespaces )
                {
                    show2 = true;
                }
            }
            else
            {
                if( has_whitespaces )
                {
                    show1 = true;
                }
            }

            UITaskHelper.Invoke( this,
                ( ) =>
                {
                    if( show1 && lblWhitespaceWarning1.Parent == null ) lblWarnings.Inlines.Add( lblWhitespaceWarning1 );
                    if( !show1 && lblWhitespaceWarning1.Parent != null ) lblWarnings.Inlines.Remove( lblWhitespaceWarning1 );
                    if( show2 && lblWhitespaceWarning2.Parent == null ) lblWarnings.Inlines.Add( lblWhitespaceWarning2 );
                    if( !show2 && lblWhitespaceWarning2.Parent != null ) lblWarnings.Inlines.Remove( lblWhitespaceWarning2 );
                } );
        }


        void SetEngineOption( IRegexEngine engine )
        {
            var cbxitem = cbxEngine.Items.Cast<ComboBoxItem>( ).Single( i => engine.CombinedId.Equals( i.Tag ) );
            cbxEngine.SelectedItem = cbxitem;

            UpdateOptions( engine );
        }


        void UpdateOptions( IRegexEngine engine )
        {
            pnlRegexOptions.Children.Clear( );
            Control options_control = engine.GetOptionsControl( );
            options_control.Width = double.NaN; // ignore sizes
            options_control.Height = double.NaN;
            pnlRegexOptions.Children.Add( options_control );

            RegexEngineCapabilityEnum caps = engine.Capabilities;
            bool groups_supported = !caps.HasFlag( RegexEngineCapabilityEnum.NoGroups );
            bool captures_supported = !caps.HasFlag( RegexEngineCapabilityEnum.NoCaptures );

            // showing unchecked checkbox when disabled
            cbShowSucceededGroupsOnly.IsEnabled = groups_supported; //?
            cbShowSucceededGroupsOnly.Visibility = groups_supported ? Visibility.Visible : Visibility.Collapsed;
            cbShowSucceededGroupsOnlyDisabledUnchecked.Visibility = !groups_supported ? Visibility.Visible : Visibility.Collapsed;

            // showing unchecked checkbox when disabled
            cbShowCaptures.IsEnabled = captures_supported; //?
            cbShowCaptures.Visibility = captures_supported ? Visibility.Visible : Visibility.Collapsed;
            cbShowCapturesDisabledUnchecked.Visibility = !captures_supported ? Visibility.Visible : Visibility.Collapsed;

            // inelegant, but works
            string? capture_note = engine.NoteForCaptures;

            runShowCapturesNote.Text = !string.IsNullOrWhiteSpace( capture_note ) ? " (" + capture_note + ")" : string.Empty;

            svOptions.ScrollToHome( );
        }


        public void ShowOverlappingMatchesWarning( bool yes )
        {
            pnlOwerlappingMatches.Visibility = yes ? Visibility.Visible : Visibility.Collapsed;
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

                    using( FindMatchesLoop ) { }
                    using( UpdateWhitespaceWarningLoop ) { }
                    using( ShowTextInfoLoop ) { }
                }

                // TODO: free unmanaged resources (unmanaged objects) and override a finalizer below.
                // TODO: set large fields to null.

                disposedValue = true;
            }
        }

        // TODO: override a finalizer only if Dispose(bool disposing) above has code to free unmanaged resources.
        // ~UCMain()
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
