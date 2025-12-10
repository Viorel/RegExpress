using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Documents;


namespace RegExpressWPFNET.Code
{
    static class Utilities
    {
        static readonly LengthConverter LengthConverter = new( );


        public static int LineNumber( [CallerLineNumber] int lineNumber = 0 )
        {
            return lineNumber;
        }


        public static string SubstringFromTo( string text, int from, int toExcluding )
        {
            from = Math.Max( 0, from );
            toExcluding = Math.Min( text.Length, toExcluding );

            if( from >= toExcluding ) return string.Empty;

            return text[from..toExcluding];
        }


        public static double PixelsFromInvariantString( string value )
        {
            return (double)LengthConverter.ConvertFromInvariantString( value )!;
        }


        public static double PointsFromInvariantString( string value )
        {
            double r = (double)LengthConverter.ConvertFromInvariantString( "1pt" )!;

            return PixelsFromInvariantString( value ) * r;
        }

        private static string[]? _commandLineArgs;
        public static bool GetCommandLineArg( String arg, out String? NextVal ) {
            NextVal=null;
            var arr = GetCommandLineArgArr( arg, 1 );
            if (arr.Length == 0)
                return false;
            NextVal = arr[0];
            return true;
        }
        /// <summary>
        /// return an array with each occurrence of the arg, allows --ignore a --ignore b
        /// </summary>
        /// <param name="arg"></param>
        /// <returns></returns>
        public static string[] GetCommandLineArgArr( string arg, int max = 0 )
        {
            _commandLineArgs ??= Environment.GetCommandLineArgs( );
            List<string> ret = new();
            bool addNext = false;
            string targetArg = "--" + arg;

            foreach( var cmdArg in _commandLineArgs )
            {
                if( cmdArg.Equals( targetArg, StringComparison.CurrentCultureIgnoreCase) )
                {
                    addNext = true;
                }
                else if( addNext )
                {
                    ret.Add( cmdArg );
                    if (ret.Count == max)
                        break;
                    addNext = false;
                }
            }

            return ret.ToArray( );
        }
        public static string? GetCommandLineArgStr( String arg )
        {
            if (! GetCommandLineArg( arg, out String? NextVal ))
                return null;
            return NextVal;
        }
        public static bool GetCommandLineExists(String arg) => GetCommandLineArg( arg, out _ );


        [Conditional( "DEBUG" )]
        public static void DbgSimpleLog( Exception exc, [CallerFilePath] string? filePath = null, [CallerMemberName] string? memberName = null, [CallerLineNumber] int lineNumber = 0 )
        {
            Debug.WriteLine( $"*** {exc.GetType( ).Name} in {memberName}:{lineNumber}" );
        }


        public static void DbgSaveXAML( string filename, FlowDocument doc )
        {
            try
            {
                var r = new TextRange( doc.ContentStart, doc.ContentEnd );

                using( var fs = File.OpenWrite( filename ) )
                {
                    r.Save( fs, DataFormats.Xaml, true );
                }
            }
            catch( Exception exc )
            {
                _ = exc;
                if (RegExpressLibrary.InternalConfig.HandleException( exc ))
                    throw;
            }
        }


        public static void DbgLoadXAML( FlowDocument doc, string filename )
        {
            try
            {
                var r = new TextRange( doc.ContentStart, doc.ContentEnd );

                using( var fs = File.OpenRead( filename ) )
                {
                    r.Load( fs, DataFormats.Xaml );
                }
            }
            catch( Exception exc )
            {
                _ = exc;
                if (RegExpressLibrary.InternalConfig.HandleException( exc ))
                    throw;
            }
        }
    }
}
