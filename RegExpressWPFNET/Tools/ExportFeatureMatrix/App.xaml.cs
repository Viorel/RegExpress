using System.Configuration;
using System.Data;
using System.Windows;

namespace ExportFeatureMatrix
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        public App( )
        {
            AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
            //Dispatcher.UnhandledException += Dispatcher_UnhandledException;
            //DispatcherUnhandledException += App_DispatcherUnhandledException;
            //TaskScheduler.UnobservedTaskException += TaskScheduler_UnobservedTaskException;
        }

        private void CurrentDomain_UnhandledException( object sender, UnhandledExceptionEventArgs e )
        {
            string m;

            const int LINES_TO_SHOW = 10;

            switch( e.ExceptionObject )
            {
            case Exception exc:
                m = string.Join( Environment.NewLine, exc.ToString( ).Split( ["\r\n", "\r", "\n"], LINES_TO_SHOW + 1, StringSplitOptions.None ).Take( LINES_TO_SHOW ) );
                break;
            case null:
                m = "";
                break;
            case object obj:
                m = obj.GetType( ).FullName!;
                break;
            }

            MessageBox.Show(
                "Unhandled exception has occurred." + Environment.NewLine + Environment.NewLine + m,
                "Regex Feature Matrix",
                MessageBoxButton.OK,
                MessageBoxImage.Error
                );
        }
    }

}
