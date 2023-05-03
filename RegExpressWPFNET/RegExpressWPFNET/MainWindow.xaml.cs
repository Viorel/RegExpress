using System;
using System.Collections.Generic;
using System.Configuration;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.DirectoryServices.ActiveDirectory;
using System.Globalization;
using System.IO;
using System.IO.IsolatedStorage;
using System.Linq;
using System.Media;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
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
using System.Xml;
using RegExpressLibrary;
using RegExpressWPFNET.Code;
using Path = System.IO.Path;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        readonly ResumableLoop AutoSaveLoop;

        bool IsFullyLoaded = false;

        public static readonly RoutedUICommand NewTabCommand = new( );
        public static readonly RoutedUICommand CloseTabCommand = new( );
        public static readonly RoutedUICommand DuplicateTabCommand = new( );
        public static readonly RoutedUICommand GoToOptionsCommand = new( );
        public static readonly RoutedUICommand MoveTabLeftCommand = new( );
        public static readonly RoutedUICommand MoveTabRightCommand = new( );
        public static readonly RoutedUICommand ExportFeatureMatrixCommand = new( );

        readonly List<IRegexPlugin> mRegexPlugins = new( );
        static readonly JsonSerializerOptions JsonOptions = new( ) { AllowTrailingCommas = true, IncludeFields = true, ReadCommentHandling = JsonCommentHandling.Skip, WriteIndented = true };

        public MainWindow( )
        {
            InitializeComponent( );

            textBlockInfo.Visibility = Visibility.Visible;
            tabControl.Visibility = Visibility.Hidden;

            var MIN_INTERVAL = TimeSpan.FromSeconds( 5 );
            var interval = Properties.Settings.Default.AutoSaveInterval;
            if( interval < MIN_INTERVAL ) interval = MIN_INTERVAL;

            AutoSaveLoop = new ResumableLoop( AutoSaveThreadProc, (int)interval.TotalMilliseconds );
        }


        private void Window_SourceInitialized( object sender, EventArgs e )
        {
            TryRestoreWindowPlacement( );
            RestoreMaximisedState( );
        }


        private async void Window_Loaded( object sender, RoutedEventArgs e )
        {
            if( IsFullyLoaded ) return;
            IsFullyLoaded = true;

            Debug.Assert( textBlockInfo.Visibility == Visibility.Visible );
            Debug.Assert( tabControl.Visibility != Visibility.Visible );
            Debug.Assert( !mRegexPlugins.Any( ) );

            // --- Load plugins

            DateTime start_time = DateTime.UtcNow;

            string[]? plugin_paths;
            string exe_path = Path.GetDirectoryName( Assembly.GetEntryAssembly( )!.Location )!;

            try
            {
                string plugins_path = Path.Combine( exe_path, "Plugins.json" );
                Debug.WriteLine( $"Loading \"{plugins_path}\"..." );

                using FileStream plugins_stream = File.OpenRead( plugins_path );

                plugin_paths = await JsonSerializer.DeserializeAsync<string[]>( plugins_stream, JsonOptions );
                Debug.WriteLine( $"Total {plugin_paths!.Length} paths" );
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                MessageBox.Show( $"Failed to load plugins.\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );

                Close( );
                return;
            }

            foreach( var plugin_path in plugin_paths! )
            {
                var plugin_absolute_path = Path.Combine( exe_path, plugin_path );
                try
                {
                    Debug.WriteLine( $"Trying to load plugin \"{plugin_absolute_path}\"..." );

                    PluginLoadContext load_context = new( plugin_absolute_path );

                    var assembly = load_context.LoadFromAssemblyName( new AssemblyName( Path.GetFileNameWithoutExtension( plugin_absolute_path ) ) );

                    var plugin_type = typeof( IRegexPlugin );

                    foreach( Type type in assembly.GetTypes( ) )
                    {
                        if( plugin_type.IsAssignableFrom( type ) )
                        {
                            try
                            {
                                Debug.WriteLine( $"Making plugin \"{type.FullName}\"..." );
                                IRegexPlugin plugin = (IRegexPlugin)Activator.CreateInstance( type )!;
                                mRegexPlugins.Add( plugin );
                            }
                            catch( Exception exc )
                            {
                                if( Debugger.IsAttached ) Debugger.Break( );

                                MessageBox.Show( $"Failed to create plugin \"{plugin_path}\".\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );
                            }
                        }
                    }

                }
                catch( Exception exc )
                {
                    if( Debugger.IsAttached ) Debugger.Break( );

                    MessageBox.Show( $"Failed to load plugin \"{plugin_path}\".\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );
                }
            }

#if DEBUG
            Debug.WriteLine( $"Total plugins: {mRegexPlugins.Count}" );

            foreach( var p in mRegexPlugins )
            {
                foreach( var eng in p.GetEngines( ) )
                {
                    Debug.WriteLine( $"   {eng.Kind} {eng.Version}" );
                }
            }
#endif


            // --- Load saved data

            AllTabData? all_tab_data = null;
            string my_file = GetMyDataFile( );

            try
            {
                using( var s = File.OpenRead( my_file ) )
                {
                    all_tab_data = JsonSerializer.Deserialize<AllTabData>( s, JsonOptions );
                }
            }
            catch( DirectoryNotFoundException )
            {
                // ignore
            }
            catch( FileNotFoundException )
            {
                // ignore
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );
            }

#if DEBUG
            // (https://github.com/dotnet/wpf/issues/6440, https://stackoverflow.com/questions/14419248/cannot-find-source-for-binding)
            tabInitial.Template = null;
#endif

            tabControl.Items.Remove( tabInitial );

            if( all_tab_data == null || !all_tab_data.Tabs.Any( ) )
            {
                // No saved data

                AddNewTab( null );
            }
            else
            {
                Debug.Assert( all_tab_data.Tabs.Any( ) );

                TabItem? first_tab = null;

                foreach( var tab_data in all_tab_data.Tabs )
                {
                    TabItem tab = AddNewTab( tab_data );

                    first_tab ??= tab;
                }

                if( first_tab != null ) tabControl.SelectedItem = first_tab;
            }

            // --- Delay effect

            const int minimum_elapsed = 555;

            var elapsed = DateTime.UtcNow - start_time;

            if( elapsed.TotalMilliseconds < minimum_elapsed )
            {
                await Task.Delay( TimeSpan.FromMilliseconds( minimum_elapsed - elapsed.TotalMilliseconds ) );
            }

            textBlockInfo.Visibility = Visibility.Collapsed;
            tabControl.Visibility = Visibility.Visible;
        }


        private void tabControlMain_SelectionChanged( object sender, SelectionChangedEventArgs e )
        {
            if( !IsFullyLoaded ) return;

            TabItem? old_tab_item = e.RemovedItems?.AsQueryable( ).OfType<TabItem>( ).SingleOrDefault( );
            UCMain? old_uc_main = old_tab_item?.Content as UCMain;

            TabItem? new_tab_item = e.AddedItems?.AsQueryable( ).OfType<TabItem>( ).SingleOrDefault( );
            UCMain? new_uc_main = new_tab_item?.Content as UCMain;

            if( old_uc_main != null && new_uc_main != null )
            {
                var old_metrics = old_uc_main.GetMetrics( );
                new_uc_main.ApplyMetrics( old_metrics, full: false );
            }
        }


        private void Window_Closing( object sender, System.ComponentModel.CancelEventArgs e )
        {
            AutoSaveLoop.SignalRewind( );

            try
            {
                SaveAllTabData( );
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );
                else Debug.Fail( exc.Message, exc.ToString( ) );

                // ignore
            }

            SaveWindowPlacement( );
        }


        #region Commands

        private void NewTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void NewTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            AddNewTab( null );
        }


        private void CloseTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = ( tabControl.SelectedItem as TabItem )?.Content is UCMain;
        }


        private void CloseTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            TabItem? tab_item = ( e.Parameter as TabItem ) ?? tabControl.SelectedItem as TabItem;

            if( tab_item != null && tab_item.Content is UCMain )
            {
                CloseTab( tab_item );
            }
            else
            {
                SystemSounds.Beep.Play( );
            }
        }


        private void DuplicateTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void DuplicateTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            DuplicateTab( );
        }

        private void GoToOptionsCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void GoToOptionsCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            GoToOptions( );
        }


        private void MoveTabLeftCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true; // (checking performed by command)
        }


        private void MoveTabLeftCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            TabItem? current_tab = tabControl.SelectedItem as TabItem;

            if( current_tab == null || current_tab.Content is not UCMain )
            {
                SystemSounds.Beep.Play( );

                return;
            }

            int prev_tab_index = -1;

            for( int i = 0; i < tabControl.Items.IndexOf( current_tab ); i++ )
            {
                TabItem tab = (TabItem)tabControl.Items[i];

                if( !tab.IsVisible ) continue;
                if( tab == tabItemNew ) continue;

                prev_tab_index = i;
            }

            if( prev_tab_index < 0 )
            {
                SystemSounds.Beep.Play( );

                return;
            }

            tabControl.Items.Remove( current_tab );
            tabControl.Items.Insert( prev_tab_index, current_tab );

            RenumberTabs( );

            current_tab.IsSelected = true;
        }


        private void MoveTabRightCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true; // (checking performed by command)
        }


        private void MoveTabRightCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            TabItem? current_tab = tabControl.SelectedItem as TabItem;

            if( current_tab == null || current_tab.Content is not UCMain )
            {
                SystemSounds.Beep.Play( );

                return;
            }

            int next_tab_index = -1;

            for( int i = tabControl.Items.IndexOf( current_tab ) + 1; i < tabControl.Items.Count; i++ )
            {
                TabItem tab = (TabItem)tabControl.Items[i];

                if( !tab.IsVisible ) continue;
                if( tab == tabItemNew ) continue;

                next_tab_index = i;
                break;
            }

            if( next_tab_index < 0 )
            {
                SystemSounds.Beep.Play( );

                return;
            }

            tabControl.Items.Remove( current_tab );
            tabControl.Items.Insert( next_tab_index, current_tab );

            RenumberTabs( );

            current_tab.IsSelected = true;
        }


        private void ExportFeatureMatrixCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void ExportFeatureMatrixCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            try
            {
                var engines = CreateEngines( );

                string path = Path.Combine( Path.GetTempPath( ), Guid.NewGuid( ).ToString( "N" ) + ".html" );

                using( var xw = XmlWriter.Create( path, new XmlWriterSettings { CloseOutput = true, Indent = true, OmitXmlDeclaration = true } ) )
                {
                    FeatureMatrixUtilities.ExportAsHtml( xw, engines );
                }

                var psi = new ProcessStartInfo
                {
                    FileName = path,
                    UseShellExecute = true,
                };

                Process.Start( psi );
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                MessageBox.Show( exc.Message, "ERROR", MessageBoxButton.OKCancel, MessageBoxImage.Error );
            }
        }

        #endregion


        IReadOnlyList<IRegexEngine> CreateEngines( )
        {
            return mRegexPlugins.SelectMany( p => p.GetEngines( ) ).ToArray( );
        }


        void UCMain_Changed( object? sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;

            AutoSaveLoop.SignalWaitAndExecute( );
        }

        void AutoSaveThreadProc( ICancellable cnc )
        {
            Dispatcher.InvokeAsync( SaveAllTabData, DispatcherPriority.ApplicationIdle );
        }

        static string GetMyDataFile( )
        {
            string user_config_path = ConfigurationManager.OpenExeConfiguration( ConfigurationUserLevel.PerUserRoamingAndLocal ).FilePath;
            string my_file = Path.Combine( Path.GetDirectoryName( user_config_path )!, "RegExpressData.json" );

            return my_file;
        }


        void SaveAllTabData( )
        {
            try
            {
                var all_data = new AllTabData( );
                var uc_main_controls = GetMainTabs( ).Select( t => (UCMain)t.Content );

                foreach( var uc_main in uc_main_controls )
                {
                    var tab_data = new TabData( );
                    uc_main!.ExportTabData( tab_data );
                    all_data.Tabs.Add( tab_data );
                }

                string json = JsonSerializer.Serialize( all_data, JsonOptions );
                string my_file = GetMyDataFile( );

                try
                {
                    File.WriteAllText( my_file, json );
                }
                catch( DirectoryNotFoundException )
                {
                    // on first launch, before settings are saved, the folder is missing;
                    // try to create it, then re-save the data

                    var fi = new FileInfo( my_file );
                    fi.Directory!.Create( );

                    File.WriteAllText( my_file, json );
                }

                /*
                // An alternative (different folder)
                using( IsolatedStorageFile user_store = IsolatedStorageFile.GetUserStoreForApplication( ) )
                {
                    using( IsolatedStorageFileStream f = user_store.CreateFile( "RegExpressData.json" ) )
                    {
                        Debug.WriteLine( $"Saving data to \"{f.Name}\"..." );

                        using( var sw = new StreamWriter( f ) )
                        {
                            sw.WriteLine( json );
                        }
                    }
                }
                */
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }
        }


        UCMain? GetActiveUCMain( )
        {
            TabItem? selected_tab_item = tabControl.IsVisible ? tabControl.SelectedItem as TabItem : null;

            if( selected_tab_item != null && selected_tab_item.Content is UCMain uc_main )
            {
                return uc_main;
            }

            return null;
        }



        #region Placement

        void SaveWindowPlacement( )
        {
            try
            {
                Properties.Settings.Default.IsMaximised = WindowState == WindowState.Maximized;
                if( Properties.Settings.Default.IsMaximised )
                {
                    Properties.Settings.Default.RestoreBoundsXY = new System.Drawing.Point( (int)RestoreBounds.Location.X, (int)RestoreBounds.Location.Y );
                    Properties.Settings.Default.RestoreBoundsWH = new System.Drawing.Size( (int)RestoreBounds.Size.Width, (int)RestoreBounds.Size.Height );
                }
                else
                {
                    Properties.Settings.Default.RestoreBoundsXY = new System.Drawing.Point( (int)Left, (int)Top );
                    Properties.Settings.Default.RestoreBoundsWH = new System.Drawing.Size( (int)ActualWidth, (int)ActualHeight );
                }

                var uc_main = GetActiveUCMain( );
                if( uc_main != null )
                {
                    TabMetrics metrics = uc_main.GetMetrics( );

                    Properties.Settings.Default.McsRightWidth = metrics.RightColumnWidth;
                    Properties.Settings.Default.McsTopHeight = metrics.TopRowHeight;
                    Properties.Settings.Default.McsBottomHeight = metrics.BottomRowHeight;
                }

                Properties.Settings.Default.Save( );
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );
                else Debug.Fail( exc.Message, exc.ToString( ) );

                // ignore
            }
        }


        [StructLayout( LayoutKind.Sequential )]
        struct POINT
        {
            public Int32 X;
            public Int32 Y;
        }


        [DllImport( "user32", SetLastError = true )]
        static extern IntPtr MonitorFromPoint( POINT pt, Int32 dwFlags );

        const Int32 MONITOR_DEFAULTTONULL = 0;


        static bool IsVisibleOnAnyMonitor( Point px )
        {
            POINT p = new( ) { X = (int)px.X, Y = (int)px.Y };

            return MonitorFromPoint( p, MONITOR_DEFAULTTONULL ) != IntPtr.Zero;
        }


        Point ToPixels( Point p )
        {
            Matrix transform = PresentationSource.FromVisual( this ).CompositionTarget.TransformFromDevice;

            Point r = transform.Transform( p );

            return r;
        }


        void TryRestoreWindowPlacement( )
        {
            try
            {
                Rect r = new( Properties.Settings.Default.RestoreBoundsXY.X, Properties.Settings.Default.RestoreBoundsXY.Y, Properties.Settings.Default.RestoreBoundsWH.Width, Properties.Settings.Default.RestoreBoundsWH.Height );

                if( !r.IsEmpty && r.Width > 0 && r.Height > 0 )
                {
                    // check if the window is in working area
                    // TODO: check if it works with different DPIs

                    Point p1, p2;
                    p1 = r.TopLeft;
                    p1.Offset( 10, 10 );
                    p2 = r.TopRight;
                    p2.Offset( -10, 10 );

                    if( IsVisibleOnAnyMonitor( ToPixels( p1 ) ) || IsVisibleOnAnyMonitor( ToPixels( p2 ) ) )
                    {
                        Left = r.Left;
                        Top = r.Top;
                        Width = Math.Max( 50, r.Width );
                        Height = Math.Max( 40, r.Height );
                    }
                }
                // Note. To work on secondary monitor, the 'Maximised' state is restored in 'Window_SourceInitialized'.
            }
            catch( Exception exc )
            {
                _ = exc;
                if( Debugger.IsAttached ) Debugger.Break( );

                // ignore
            }
        }


        void RestoreMaximisedState( )
        {
            // restore the Maximised state; this works for secondary monitors as well;
            // to avoid undesirable effects, call it from 'SourceInitialised'
            if( Properties.Settings.Default.IsMaximised )
            {
                WindowState = WindowState.Maximized;
            }
        }

        #endregion

        #region Tabs

        TabItem AddNewTab( TabData? tabData )
        {
            int max =
                GetMainTabs( )
                    .Where( i => i != tabItemNew && i.Header is string )
                    .Select( i =>
                    {
                        var m = HeaderParserRegex( ).Match( (string)i.Header );
                        if( m.Success )
                        {
                            return int.Parse( m.Groups[1].Value, CultureInfo.InvariantCulture );
                        }
                        else
                        {
                            return 0;
                        }
                    } )
                    .Concat( new[] { 0 } )
                    .Max( );

            var new_tab_item = new TabItem
            {
                //Header = string.IsNullOrWhiteSpace( tab_data?.Name ) ? $"Tab {max + 1}" : tab_data.Name;
                Header = $"Regex {max + 1}",
                HeaderTemplate = (DataTemplate)tabControl.Resources["TabTemplate"]
            };

            var uc_main = new UCMain( CreateEngines( ) )
            {
                Width = double.NaN,
                Height = double.NaN
            };

            new_tab_item.Content = uc_main;

            tabControl.Items.Insert( tabControl.Items.IndexOf( tabItemNew ), new_tab_item );

            if( tabData != null ) uc_main.ApplyTabData( tabData );

            uc_main.Changed += UCMain_Changed;

            tabControl.SelectedItem = new_tab_item; //?

            return new_tab_item;
        }


        void CloseTab( TabItem tabItem )
        {
            Debug.Assert( tabItem.Content is UCMain );

            tabControl.SelectedItem = tabItem;

            TabItem[] main_tabs = GetMainTabs( ).ToArray( );
            int index = Array.IndexOf( main_tabs, tabItem );

            var r = MessageBox.Show( this,
                //main_tabs.Count( ) == 1 ? "Clear this tab?" : "Remove this tab?",
                "Remove this tab?",
                "WARNING",
                MessageBoxButton.OKCancel, MessageBoxImage.Exclamation,
                MessageBoxResult.OK, MessageBoxOptions.None );

            if( r != MessageBoxResult.OK ) return;


            UCMain uc_main = (UCMain)tabItem.Content;

            uc_main.Shutdown( );

#if DEBUG
            // (https://github.com/dotnet/wpf/issues/6440, https://stackoverflow.com/questions/14419248/cannot-find-source-for-binding)
            tabItem.Template = null;
#endif

            tabControl.Items.Remove( tabItem );

            TabItem? tab_item_to_select = null;

            if( index + 1 < main_tabs.Length )
            {
                tab_item_to_select = main_tabs[index + 1];
            }
            else
            {
                if( index - 1 >= 0 )
                {
                    tab_item_to_select = main_tabs[index - 1];
                }
            }

            if( tab_item_to_select == null )
            {
                tab_item_to_select = AddNewTab( null );
            }

            tabControl.SelectedItem = tab_item_to_select;

            RenumberTabs( );
        }


        void RenumberTabs( )
        {
            int i = 0;
            foreach( var tab in GetMainTabs( ) )
            {
                var name = "Regex " + ( ++i );
                if( !name.Equals( tab.Header ) ) tab.Header = name; // ('If' to avoid effects)
            }
        }


        IEnumerable<TabItem> GetMainTabs( )
        {
            return tabControl.Items.OfType<TabItem>( ).Where( t => t.Visibility == Visibility.Visible && t.Content is UCMain );
        }


        void DuplicateTab( )
        {
            TabItem? new_tab_item = null;
            var tab_data = new TabData( );

            if( tabControl.SelectedItem is TabItem selected_tab_item && selected_tab_item.Content is UCMain uc_main )
            {
                uc_main.ExportTabData( tab_data );
                new_tab_item = AddNewTab( tab_data );

                if( tabControl.Items.IndexOf( new_tab_item ) != tabControl.Items.IndexOf( selected_tab_item ) + 1 )
                {
                    tabControl.Items.Remove( new_tab_item );
                    int i = tabControl.Items.IndexOf( selected_tab_item );
                    tabControl.Items.Insert( i + 1, new_tab_item );
                }
            }

            if( new_tab_item == null )
            {
                SystemSounds.Beep.Play( );
            }
            else
            {
                tabControl.SelectedItem = new_tab_item;

                RenumberTabs( );
            }
        }


        void GoToOptions( )
        {
            var uc_main = GetActiveUCMain( );

            if( uc_main == null )
            {
                Debug.Assert( false );
            }
            else
            {
                uc_main.GoToOptions( );
            }
        }


        [GeneratedRegex( @"^Regex\s*(\d+)$" )]
        private static partial Regex HeaderParserRegex( );

        #endregion

    }
}
