using System.Diagnostics;
using System.IO;
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

            Properties.Settings.Default.EnginesJsonPath = ofd.FileName;
            Properties.Settings.Default.Save( );
        }

        private void rbOutputExcel_Checked( object sender, RoutedEventArgs e )
        {
            tbOutputFile.Text = Properties.Settings.Default.OutputExcelPath;
            Properties.Settings.Default.OutputType = (int)OutputTypeEnum.Excel;
            Properties.Settings.Default.Save( );
        }

        private void rbOutputHtml_Checked( object sender, RoutedEventArgs e )
        {
            tbOutputFile.Text = Properties.Settings.Default.OutputHtmlPath;
            Properties.Settings.Default.OutputType = (int)OutputTypeEnum.Html;
            Properties.Settings.Default.Save( );
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

            Properties.Settings.Default.Save( );

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
            try
            {
                tblProgress.Text = "";

                if( !ValidateInput( ) ) return;

                OutputTypeEnum output_type = GetOutputType( );
                Debug.Assert( output_type != OutputTypeEnum.None );

                if( checkBoxVerify.IsChecked == true && output_type != OutputTypeEnum.Excel )
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
                    this.IsEnabled = false;

                    switch( output_type )
                    {
                    case OutputTypeEnum.Excel:
                    {
                        ExporterToExcel exporter = new( );

                        exporter.Export( tbOutputFile.Text, plugins!, checkBoxVerify.IsChecked == true );
                    }
                    break;
                    case OutputTypeEnum.Html:
                    {
                        ExporterToHtml exporter = new( );

                        exporter.Export( tbOutputFile.Text, plugins! );
                    }
                    break;
                    default:
                        throw new NotSupportedException( $"Output type not supported: '{output_type}'" );
                    }
                }
                finally
                {
                    this.IsEnabled = true;
                }

                tblProgress.Text = "DONE.";

                if( MessageBox.Show( this, "The file was created.\r\n\r\nOpen it?", CAPTION, MessageBoxButton.OKCancel, MessageBoxImage.Question, MessageBoxResult.Yes ) == MessageBoxResult.OK )
                {
                    Process process = new( );
                    process.StartInfo.FileName = tbOutputFile.Text;
                    process.StartInfo.UseShellExecute = true;

                    process.Start( );
                }

                tblProgress.Text = "";
            }
            catch( Exception exc )
            {
                if( Debugger.IsAttached ) Debugger.Break( );

                MessageBox.Show( this, exc.Message, CAPTION, MessageBoxButton.OK, MessageBoxImage.Error );

                tblProgress.Text = "";
            }
        }

        private void buttonClose_Click( object sender, RoutedEventArgs e )
        {
            Close( );
        }
    }
}
