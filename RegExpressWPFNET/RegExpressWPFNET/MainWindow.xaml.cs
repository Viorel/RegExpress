using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.DirectoryServices.ActiveDirectory;
using System.Globalization;
using System.IO;
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
using RegExpressLibrary;
using RegExpressWPFNET.Code;


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

        readonly List<IRegexPlugin> mRegexPlugins = new( );

        public MainWindow( )
        {
            InitializeComponent( );

            textBlockInfo.Visibility = Visibility.Visible;
            tabControl.Visibility = Visibility.Collapsed;

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
            Debug.Assert( tabControl.Visibility == Visibility.Collapsed );
            Debug.Assert( !mRegexPlugins.Any( ) );

            // --- Load plugins

            DateTime start_time = DateTime.UtcNow;

            string[]? plugin_paths;
            string exe_path = System.IO.Path.GetDirectoryName( Assembly.GetEntryAssembly( )!.Location )!;

            try
            {
                string plugins_path = System.IO.Path.Combine( exe_path, "Plugins.json" );
                Debug.WriteLine( $"Loading \"{plugins_path}\"..." );

                using FileStream plugins_stream = File.OpenRead( plugins_path );

                plugin_paths = await JsonSerializer.DeserializeAsync<string[]>( plugins_stream );
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
                var plugin_absolute_path = System.IO.Path.Combine( exe_path, plugin_path );
                try
                {
                    Debug.WriteLine( $"Trying to load plugin \"{plugin_absolute_path}\"..." );

                    PluginLoadContext load_context = new( plugin_absolute_path );

                    var assembly = load_context.LoadFromAssemblyName( new AssemblyName( System.IO.Path.GetFileNameWithoutExtension( plugin_absolute_path ) ) );

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

            //...

            if( false )
            {

            }
            else
            {
                // No saved data

                //TabItem new_tab_item = new( )
                //{
                //    Header = "Regex 1",
                //    HeaderTemplate = (DataTemplate)tabControl.Resources["TabTemplate"]
                //};
                //
                //var uc_main = new UCMain( mEngines )
                //{
                //    Width = double.NaN,
                //    Height = double.NaN
                //};
                //uc_main.Changed += UCMain_Changed;
                //
                //new_tab_item.Content = uc_main;
                //
                //tabControl.Items.Insert( tabControl.Items.IndexOf( tabItemNew ), new_tab_item );
                //tabControl.SelectedItem = new_tab_item;

                tabControl.Items.Remove( tabInitial );

                CreateTab( null );
            }



            // --- Delay effect

            const int minimum_elapsed = 1111;

            var elapsed = DateTime.UtcNow - start_time;

            if( elapsed.TotalMilliseconds < minimum_elapsed )
            {
                await Task.Delay( TimeSpan.FromMilliseconds( minimum_elapsed - elapsed.TotalMilliseconds ) );
            }

            textBlockInfo.Visibility = Visibility.Collapsed;
            tabControl.Visibility = Visibility.Visible;
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


        private void NewTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void NewTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            CreateTab( null );
        }


        private void CloseTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void CloseTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            SystemSounds.Beep.Play( );
        }


        private void DuplicateTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void DuplicateTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
        }

        private void GoToOptionsCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void GoToOptionsCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
        }


        IReadOnlyList<IRegexEngine> CreateEngines( )
        {
            return mRegexPlugins.SelectMany( p => p.GetEngines( ) ).ToArray( );
        }


        void UCMain_Changed( object sender, EventArgs e )
        {
            if( !IsFullyLoaded ) return;

            AutoSaveLoop.SignalWaitAndExecute( );
        }

        void AutoSaveThreadProc( ICancellable cnc )
        {
            Dispatcher.InvokeAsync( SaveAllTabData, DispatcherPriority.ApplicationIdle );
        }

        void SaveAllTabData( )
        {
            //........
        }

        UCMain? GetActiveUCMain( )
        {
            TabItem? selected_tab_item = tabControl.IsVisible ? tabControl.SelectedItem as TabItem : null;

            if( selected_tab_item != null && selected_tab_item.Content is UCMain )
            {
                return (UCMain)selected_tab_item.Content;
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
            POINT p = new POINT { X = (int)px.X, Y = (int)px.Y };

            return MonitorFromPoint( p, MONITOR_DEFAULTTONULL ) != IntPtr.Zero;
        }


        Point ToPixels( Point p )
        {
            Matrix transform = PresentationSource.FromVisual( this ).CompositionTarget.TransformFromDevice;

            Point r = transform.Transform( p );

            return r;
        }


        [SuppressMessage( "Design", "CA1031:Do not catch general exception types", Justification = "<Pending>" )]
        void TryRestoreWindowPlacement( )
        {
            try
            {
                Rect r = new Rect( Properties.Settings.Default.RestoreBoundsXY.X, Properties.Settings.Default.RestoreBoundsXY.Y, Properties.Settings.Default.RestoreBoundsWH.Width, Properties.Settings.Default.RestoreBoundsWH.Height );

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

        TabItem CreateTab( TabData? tabData )
        {
            int max =
                tabControl.Items
                    .OfType<TabItem>( )
                    .Where( i => i != tabItemNew && i.Header is string )
                    .Select( i =>
                    {
                        var m = Regex.Match( (string)i.Header, @"^Regex\s*(\d+)$" );
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


            var new_tab_item = new TabItem( );
            //new_tab_item.Header = string.IsNullOrWhiteSpace( tab_data?.Name ) ? $"Tab {max + 1}" : tab_data.Name;
            new_tab_item.Header = $"Regex {max + 1}";
            new_tab_item.HeaderTemplate = (DataTemplate)tabControl.Resources["TabTemplate"];

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

        #endregion

    }
}
