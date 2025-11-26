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
