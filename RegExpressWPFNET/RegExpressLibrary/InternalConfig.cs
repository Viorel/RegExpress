using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Windows;

namespace RegExpressLibrary
{
    public static class InternalConfig
    {
        public static bool SHOW_DEBUG_BUTTONS = false;
        public static bool DEBUG_LOG_AI_MESSAGES = true;

        /// <summary>
        /// as the target text can be long lets not keep the old versions around two options are update the target text with the new text, or remove the old one add a note and add the new one
        /// </summary>
        public static bool DONT_UPDATE_OLD_AI_TEXT_MARK_REMOVED = true;
        public static string[]? limited_engine_dlls;
        [Flags]
        public enum ON_EXCEPTION_ACTION
        {
            None,
            MessageBox = 1 << 0,
            DebuggerBreak = 1 << 1,
            Rethrow = 1 << 2,
            IncludeStackTrace = 1 << 3,
            IncludeTraceDetails = 1 << 4,

        }
        public static ON_EXCEPTION_ACTION ON_EXCEPTION_DEBUGGER_ATTACHED = ON_EXCEPTION_ACTION.DebuggerBreak | ON_EXCEPTION_ACTION.IncludeTraceDetails | ON_EXCEPTION_ACTION.IncludeStackTrace;
        public static ON_EXCEPTION_ACTION ON_EXCEPTION_STANDARD = ON_EXCEPTION_ACTION.MessageBox;
        public static void HandleOtherCriticalError( String error, [System.Runtime.CompilerServices.CallerLineNumber] int source_line_number = 0, [System.Runtime.CompilerServices.CallerMemberName] string member_name = "", [System.Runtime.CompilerServices.CallerFilePath] string source_file_path = "" ) =>
            _CriticalError( new StringBuilder( error ), source_line_number, member_name, source_file_path );
        private static void _CriticalError( StringBuilder sb, [System.Runtime.CompilerServices.CallerLineNumber] int source_line_number = 0, [System.Runtime.CompilerServices.CallerMemberName] string member_name = "", [System.Runtime.CompilerServices.CallerFilePath] string source_file_path = "" )
        {
            var ON_EXCEPTION = Debugger.IsAttached ? ON_EXCEPTION_DEBUGGER_ATTACHED : ON_EXCEPTION_STANDARD;
            if( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.IncludeTraceDetails ) )
            {
                sb.Append( "Member: " ).AppendLine( member_name );
                sb.Append( "File: " ).AppendLine( source_file_path );
                sb.Append( "Line: " ).AppendLine( source_line_number.ToString( ) );
            }

            string message = sb.ToString( );

            // MessageBox (on UI thread if possible)
            if( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.MessageBox ) )
            {
                try
                {
                    if( Application.Current?.Dispatcher != null && !Application.Current.Dispatcher.CheckAccess( ) )
                        Application.Current.Dispatcher.Invoke( ( ) => MessageBox.Show( message, "Exception", MessageBoxButton.OK, MessageBoxImage.Error ) );
                    else
                        MessageBox.Show( message, "Exception", MessageBoxButton.OK, MessageBoxImage.Error );

                }
                catch
                {
                    // Fallback to Debug output if UI unavailable
                    Debug.WriteLine( message );
                }
            }

            // Break into debugger if requested and a debugger is attached
            if( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.DebuggerBreak ) && Debugger.IsAttached )
                Debugger.Break( );


            // Always log to debug output for visibility
            Debug.WriteLine( message );

        }

        public static bool HandleException( String msg, Exception exception, [System.Runtime.CompilerServices.CallerLineNumber] int source_line_number = 0, [System.Runtime.CompilerServices.CallerMemberName] string member_name = "", [System.Runtime.CompilerServices.CallerFilePath] string source_file_path = "", [CallerArgumentExpression( "exception" )] string exceptionName = "" )
        {
            if( exception == null ) return false;
            var ON_EXCEPTION = Debugger.IsAttached ? ON_EXCEPTION_DEBUGGER_ATTACHED : ON_EXCEPTION_STANDARD;

            // Build a diagnostic message
            StringBuilder sb = new( );

            sb.AppendLine( "Exception! " );
            if( !String.IsNullOrWhiteSpace( msg ) )
                sb.AppendLine( msg + " " );
            if( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.IncludeTraceDetails ) )
            {
                sb.Append( "Member: " ).AppendLine( member_name );
                sb.Append( "File: " ).AppendLine( source_file_path );
                sb.Append( "Line: " ).AppendLine( source_line_number.ToString( ) );
            }
            sb.Append( "Exception Var: " ).AppendLine( exceptionName );
            sb.Append( "Type: " ).AppendLine( exception.GetType( ).FullName );
            sb.Append( "Message: " ).AppendLine( exception.Message );

            if( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.IncludeStackTrace ) )
            {
                var st = exception.StackTrace;
                if( !string.IsNullOrWhiteSpace( st ) )
                {
                    sb.AppendLine( "StackTrace:" );
                    sb.AppendLine( st );
                }
            }
            _CriticalError( sb, source_line_number, member_name, source_file_path );

            return ( ON_EXCEPTION.HasFlag( ON_EXCEPTION_ACTION.Rethrow ) );
        }
        /// <summary>
        /// Returns true if caller should rethrow the exception
        /// </summary>
        /// <param name="exception"></param>
        /// <param name="source_line_number"></param>
        /// <param name="member_name"></param>
        /// <param name="source_file_path"></param>
        /// <param name="exceptionName"></param>
        /// <returns></returns>
        public static bool HandleException( Exception exception, [System.Runtime.CompilerServices.CallerLineNumber] int source_line_number = 0, [System.Runtime.CompilerServices.CallerMemberName] string member_name = "", [System.Runtime.CompilerServices.CallerFilePath] string source_file_path = "", [CallerArgumentExpression( "exception" )] string exceptionName = "" ) =>
            HandleException( string.Empty, exception, source_line_number, member_name, source_file_path, exceptionName );

    }
}
