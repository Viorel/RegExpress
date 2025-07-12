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
using RegExpressLibrary;

namespace ExportFeatureMatrix
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow( )
        {
            InitializeComponent( );
        }

        private async void buttonExportExcel_Click( object sender, RoutedEventArgs e )
        {
            // Load engines

            string engines_json_path = @"..\..\..\..\..\RegExpressWPFNET\bin\Debug\net9.0-windows7.0\Engines.json"; // TODO: browse

            IReadOnlyList<RegexPlugin>? plugins = await PluginLoader.LoadEngines( this, engines_json_path );

            if( plugins == null ) return;

            if( plugins.Count == 0 )
            {
                MessageBox.Show( this, "No engines." );

                return;
            }

            //MessageBox.Show( this, $"{engines?.Count ?? 0} plugins" );

            FeatureMatrixExporter exporter = new( );

            string output_Excel_path = "FeatureMatrix.xlsx"; // TODO: browse

            exporter.ExportToExcel( output_Excel_path, plugins! );

            Close( );
        }



    }
}