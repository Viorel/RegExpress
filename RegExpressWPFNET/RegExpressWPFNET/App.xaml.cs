using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;


namespace RegExpressWPFNET
{
    /// <summary>
    /// Interaction logic for App.xaml
    /// </summary>
    public partial class App : Application
    {
        [DllImport( "user32" )]
        static extern bool IsIconic( IntPtr hWnd );

        [DllImport( "user32" )]
        static extern bool ShowWindow( IntPtr hWnd, int cmdShow );
        const int SW_RESTORE = 9;

        [DllImport( "user32" )]
        static extern bool SetForegroundWindow( IntPtr hWnd );


        public App( )
        {
            AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
            Dispatcher.UnhandledException += Dispatcher_UnhandledException;
            DispatcherUnhandledException += App_DispatcherUnhandledException;
            TaskScheduler.UnobservedTaskException += TaskScheduler_UnobservedTaskException;
        }

        private void App_Startup( object sender, StartupEventArgs e )
        {
            Process current_process = Process.GetCurrentProcess( );

            Process? other_process = Process.GetProcessesByName( current_process.ProcessName ).FirstOrDefault( p => p.Id != current_process.Id && p.MainWindowHandle != IntPtr.Zero );

            if( other_process != null )
            {
                if( IsIconic( other_process.MainWindowHandle ) ) ShowWindow( other_process.MainWindowHandle, SW_RESTORE );

                SetForegroundWindow( other_process.MainWindowHandle );

                Shutdown( );

                return;
            }

            other_process = Process.GetProcessesByName( current_process.ProcessName ).FirstOrDefault( p => p.Id < current_process.Id );

            if( other_process != null )
            {
                Shutdown( );

                return;
            }
        }


        private void CurrentDomain_UnhandledException( object sender, UnhandledExceptionEventArgs e )
        {
            string m;

            const int LINES_TO_SHOW = 10;

            switch( e.ExceptionObject )
            {
            case Exception exc:
                m = string.Join( Environment.NewLine, exc.ToString( ).Split( new[] { "\r\n", "\r", "\n" }, LINES_TO_SHOW + 1, StringSplitOptions.None ).Take( LINES_TO_SHOW ) );
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
                "RegExpress Error",
                MessageBoxButton.OK,
                MessageBoxImage.Error
                );
        }

        private void TaskScheduler_UnobservedTaskException( object? sender, UnobservedTaskExceptionEventArgs e )
        {
            if (RegExpressLibrary.InternalConfig.HandleException( e.Exception ))
                    throw e.Exception;
        }

        private void App_DispatcherUnhandledException( object sender, DispatcherUnhandledExceptionEventArgs e )
        {
            if (RegExpressLibrary.InternalConfig.HandleException( e.Exception ))
                    throw e.Exception;
        }

        private void Dispatcher_UnhandledException( object sender, DispatcherUnhandledExceptionEventArgs e )
        {
            if (RegExpressLibrary.InternalConfig.HandleException( e.Exception ))
                    throw e.Exception;
        }
    }
}
