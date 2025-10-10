using System.Diagnostics;
using System.IO;
using System.Media;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Microsoft.Win32;
using RegExpressLibrary;

namespace ExportFeatureMatrix
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        const string CAPTION = "Regex Feature Matrix";

        enum OutputTypeEnum
        {
            None,
            Excel,
            Html,
        }

        Thread? mThread = null;

        public MainWindow( )
        {
            InitializeComponent( );
        }

        private void Window_Loaded( object sender, RoutedEventArgs e )
        {
            tbEnginesFile.Text = Properties.Settings.Default.EnginesJsonPath;

            switch( Properties.Settings.Default.OutputType )
            {
            case (int)OutputTypeEnum.Excel:
                rbOutputExcel.IsChecked = true;
                break;
            case (int)OutputTypeEnum.Html:
                rbOutputHtml.IsChecked = true;
                break;
            }

            tblProgress.Text = "";
        }

        private void btnBrowseEnginesJsonFile_Click( object sender, RoutedEventArgs e )
        {
            OpenFileDialog ofd = new( )
            {
                FileName = !string.IsNullOrWhiteSpace( tbEnginesFile.Text ) ? tbEnginesFile.Text : "Engines.json",
                DefaultExt = ".json",
                Filter = "Json files (.json)|*.json|All Files|*.*",
                CheckPathExists = true,
                CheckFileExists = true,
            };

            if( ofd.ShowDialog( this ) != true ) return;

            tbEnginesFile.Text = ofd.FileName;
        }

        private void rbOutputExcel_Checked( object sender, RoutedEventArgs e )
        {
            tbOutputFile.Text = Properties.Settings.Default.OutputExcelPath;
        }

        private void rbOutputHtml_Checked( object sender, RoutedEventArgs e )
        {
            tbOutputFile.Text = Properties.Settings.Default.OutputHtmlPath;
        }

        private void btnBrowseOutputFile_Click( object sender, RoutedEventArgs e )
        {
            OutputTypeEnum output_type = GetOutputType( );

            if( output_type == OutputTypeEnum.None )
            {
                MessageBox.Show( this, "Please select the output type", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                return;
            }

            string suggested_output_name;
            string ext;
            string filter;

            switch( output_type )
            {
            case OutputTypeEnum.Excel:
                suggested_output_name = "FeatureMatrix.xlsx";
                ext = ".xlsx";
                filter = "Excel files (.xlsx)|*.xlsx|All Files|*.*";
                break;
            case OutputTypeEnum.Html:
                suggested_output_name = "FeatureMatrix.html";
                ext = ".html";
                filter = "HTML files (.html)|*.html|All Files|*.*";
                break;
            default:
                suggested_output_name = "";
                ext = "";
                filter = "All Files|*.*";
                break;
            }

            SaveFileDialog ofd = new( )
            {
                FileName = !string.IsNullOrWhiteSpace( tbOutputFile.Text ) ? tbOutputFile.Text : suggested_output_name,
                DefaultExt = ext,
                Filter = filter,
                CheckPathExists = true,
                CheckFileExists = false,
                OverwritePrompt = false,
            };

            if( ofd.ShowDialog( this ) != true ) return;

            switch( output_type )
            {
            case OutputTypeEnum.Excel:
                Properties.Settings.Default.OutputExcelPath = ofd.FileName;
                break;
            case OutputTypeEnum.Html:
                Properties.Settings.Default.OutputHtmlPath = ofd.FileName;
                break;
            }

            tbOutputFile.Text = ofd.FileName;
        }

        OutputTypeEnum GetOutputType( )
        {
            if( rbOutputExcel.IsChecked == true ) return OutputTypeEnum.Excel;
            if( rbOutputHtml.IsChecked == true ) return OutputTypeEnum.Html;

            return OutputTypeEnum.None;
        }

        bool ValidateInput( )
        {
            if( string.IsNullOrWhiteSpace( tbEnginesFile.Text ) )
            {
                MessageBox.Show( this, "Please select the “Engines.json” file", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                return false;
            }

            return true;
        }

        bool ValidateOutput( )
        {
            OutputTypeEnum output_type = GetOutputType( );

            if( output_type == OutputTypeEnum.None )
            {
                MessageBox.Show( this, "Please select the output type", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                return false;
            }

            if( string.IsNullOrWhiteSpace( tbOutputFile.Text ) )
            {
                MessageBox.Show( this, "Please select the output file", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                return false;
            }
            else
            {
                if( File.Exists( tbOutputFile.Text ) )
                {
                    if( MessageBox.Show( this, "The output file already exists.\r\n\r\nOverwrite?", CAPTION, MessageBoxButton.OKCancel, MessageBoxImage.Information ) != MessageBoxResult.OK ) return false;
                }
            }

            return true;
        }

        private async void buttonCreateFile_Click( object sender, RoutedEventArgs e )
        {
            if( mThread != null )
            {
                MessageBox.Show( this, "Operation is in progress.", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                return;
            }

            Properties.Settings.Default.EnginesJsonPath = tbEnginesFile.Text;
            Properties.Settings.Default.OutputType = (int)GetOutputType( );
            switch( GetOutputType( ) )
            {
            case OutputTypeEnum.Excel:
                Properties.Settings.Default.OutputExcelPath = tbOutputFile.Text;
                break;
            case OutputTypeEnum.Html:
                Properties.Settings.Default.OutputHtmlPath = tbOutputFile.Text;
                break;
            }
            Properties.Settings.Default.Save( );

            try
            {
                tblProgress.Text = "";

                bool is_verify = checkBoxVerify.IsChecked == true;

                if( !ValidateInput( ) ) return;

                OutputTypeEnum output_type = GetOutputType( );
                Debug.Assert( output_type != OutputTypeEnum.None );

                if( is_verify && output_type != OutputTypeEnum.Excel )
                {
                    MessageBox.Show( this, "Verification is only available for Excel output.", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                    return;
                }

                if( !ValidateOutput( ) ) return;

                // Load engines

                tblProgress.Text = "Loading engines...";

                string engines_json_path = tbEnginesFile.Text;

                (IReadOnlyList<RegexPlugin>? plugins, IReadOnlyList<RegexPlugin>? no_fm_plugins) = await PluginLoader.LoadEngines( this, engines_json_path );

                if( plugins == null ) return;

                if( no_fm_plugins != null ) plugins = plugins.Except( no_fm_plugins ).ToList( );

                if( plugins.Count == 0 )
                {
                    MessageBox.Show( this, "No engines.", CAPTION, MessageBoxButton.OK, MessageBoxImage.Information );

                    return;
                }

                tblProgress.Text = $"Processing {plugins.Count} engines...";

                try
                {
                    switch( output_type )
                    {
                    case OutputTypeEnum.Excel:
                    {
                        tblProgress.Text = "Starting operation...";

                        string output_file = tbOutputFile.Text;

                        void action( )
                        {
                            try
                            {
                                ExporterToExcel exporter = new( );

                                exporter.Export( output_file, plugins!, is_verify, ShowProgressOnFeatures, ShowProgressOnEngines );

                                Dispatcher.Invoke( ( ) =>
                                {
                                    SystemSounds.Exclamation.Play();

                                    tblProgress.Text = "DONE.";

                                    textBlockFeature.Visibility = progressOnFeatures.Visibility =
                                        textBlockEngine.Visibility = progressOnEngines.Visibility = Visibility.Hidden;

                                    if( MessageBox.Show( this, "The file was created.\r\n\r\nOpen it?", CAPTION, MessageBoxButton.OKCancel, MessageBoxImage.Question, MessageBoxResult.Yes ) == MessageBoxResult.OK )
                                    {
                                        Process process = new( );
                                        process.StartInfo.FileName = tbOutputFile.Text;
                                        process.StartInfo.UseShellExecute = true;

                                        process.Start( );
                                    }

                                    tblProgress.Text = "";
                                } );
                            }
                            catch( Exception exc )
                            {
                                if( Debugger.IsAttached ) Debugger.Break( );

                                string message = exc.Message;

                                Dispatcher.BeginInvoke( ( ) =>
                                {
                                    MessageBox.Show( this, exc.Message, CAPTION, MessageBoxButton.OK, MessageBoxImage.Error );
                                } );
                            }

                            mThread = null;
                        }

                        mThread = new( action )
                        {
                            IsBackground = true,
                        };

                        mThread.SetApartmentState( ApartmentState.STA );
                        mThread.Start( );

                        tblProgress.Text = "Creating file...";
                    }
                    break;
                    default:
                        throw new NotSupportedException( $"Output type not supported: '{output_type}'" );
                    }
                }
                finally
                {

                }
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                MessageBox.Show( this, exc.Message, CAPTION, MessageBoxButton.OK, MessageBoxImage.Error );

                tblProgress.Text = "";
            }

            // to make sure that DLLs of engines are unloaded and unlocked
            GC.Collect( );
            GC.WaitForPendingFinalizers( );
        }

        void ShowProgressOnFeatures( string info, int index, int total )
        {
            Dispatcher.BeginInvoke( ( ) =>
            {
                textBlockFeature.Text = info;
                progressOnFeatures.Maximum = total;
                progressOnFeatures.Value = index + 1;

                textBlockFeature.Visibility = Visibility.Visible;
                progressOnFeatures.Visibility = Visibility.Visible;
            } );
        }

        void ShowProgressOnEngines( string info, int index, int total )
        {
            Dispatcher.BeginInvoke( ( ) =>
            {
                textBlockEngine.Text = info;
                progressOnEngines.Maximum = total;
                progressOnEngines.Value = index + 1;

                textBlockEngine.Visibility = Visibility.Visible;
                progressOnEngines.Visibility = Visibility.Visible;
            } );
        }

        private void buttonClose_Click( object sender, RoutedEventArgs e )
        {
            Close( );
        }
    }
}
