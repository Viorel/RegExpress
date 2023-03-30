using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.DirectoryServices.ActiveDirectory;
using System.IO;
using System.Linq;
using System.Media;
using System.Reflection;
using System.Text;
using System.Text.Json;
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

        public static readonly RoutedUICommand NewTabCommand = new( );
        public static readonly RoutedUICommand CloseTabCommand = new( );
        public static readonly RoutedUICommand DuplicateTabCommand = new( );
        public static readonly RoutedUICommand GoToOptionsCommand = new( );

        readonly List<IRegexPlugin> mRegexPlugins = new List<IRegexPlugin>( );

        public MainWindow( )
        {
            InitializeComponent( );

            textBlockInfo.Visibility = Visibility.Visible;
            tabControl.Visibility = Visibility.Collapsed;
        }


        private async void Window_Loaded( object sender, RoutedEventArgs e )
        {
            Debug.Assert( textBlockInfo.Visibility == Visibility.Visible );
            Debug.Assert( tabControl.Visibility == Visibility.Collapsed );
            Debug.Assert( !mRegexPlugins.Any( ) );

            DateTime start_time = DateTime.UtcNow;

            string[]? plugin_paths;
            string exe_path = System.IO.Path.GetDirectoryName( Assembly.GetEntryAssembly( )!.Location )!;

            try
            {
                string plugins_path = System.IO.Path.Combine( exe_path, "Plugins.json" );
                Debug.WriteLine( $"Loading \"{plugins_path}\"..." );

                using FileStream plugins_stream = File.OpenRead( plugins_path );

                plugin_paths = await JsonSerializer.DeserializeAsync<string[]>( plugins_stream );
                Debug.WriteLine( $"Total {plugin_paths!.Count( )} paths" );
            }
            catch( Exception exc )
            {
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

                    PluginLoadContext load_context = new PluginLoadContext( plugin_absolute_path );

                    var assembly = load_context.LoadFromAssemblyName( new AssemblyName( System.IO.Path.GetFileNameWithoutExtension( plugin_absolute_path ) ) );
                }
                catch( Exception exc )
                {
                    MessageBox.Show( $"Failed to load plugin \"{plugin_path}\".\r\n\r\n{exc.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Exclamation );
                }
            }

            Debug.WriteLine( $"Total {mRegexPlugins.Count( )} plugins" );

            const int minimum_elapsed = 1111;

            var elapsed = DateTime.UtcNow - start_time;

            if( elapsed.TotalMilliseconds < minimum_elapsed )
            {
                await Task.Delay( TimeSpan.FromMilliseconds( minimum_elapsed - elapsed.TotalMilliseconds ) );
            }

            textBlockInfo.Visibility = Visibility.Collapsed;
            tabControl.Visibility = Visibility.Visible;
        }


        private void NewTabCommand_CanExecute( object sender, CanExecuteRoutedEventArgs e )
        {
            e.CanExecute = true;
        }


        private void NewTabCommand_Execute( object sender, ExecutedRoutedEventArgs e )
        {
            SystemSounds.Beep.Play( );
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

    }
}
