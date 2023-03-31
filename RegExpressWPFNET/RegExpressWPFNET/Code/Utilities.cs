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
		static readonly LengthConverter LengthConverter = new LengthConverter( );


		public static int LineNumber( [CallerLineNumber] int lineNumber = 0 )
		{
			return lineNumber;
		}


		public static string SubstringFromTo( string text, int from, int toExcluding )
		{
			from = Math.Max( 0, from );
			toExcluding = Math.Min( text.Length, toExcluding );

			if( from >= toExcluding ) return string.Empty;

			return text.Substring( from, toExcluding - from );
		}


		public static double ToPixels( string value )
		{
			return (double)LengthConverter.ConvertFromInvariantString( value );
		}


		public static double ToPoints( string value )
		{
			double r = (double)LengthConverter.ConvertFrom( "1pt" );

			return ToPixels(value) * r;
		}


		[Conditional( "DEBUG" )]
		public static void DbgSimpleLog( Exception exc, [CallerFilePath] string filePath = null, [CallerMemberName] string memberName = null, [CallerLineNumber] int lineNumber = 0 )
		{
			Debug.WriteLine( $"*** {exc.GetType( ).Name} in {memberName}:{lineNumber}" );
		}


		[System.Diagnostics.CodeAnalysis.SuppressMessage( "Design", "CA1031:Do not catch general exception types", Justification = "<Pending>" )]
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
			catch( Exception exc)
			{
				_ = exc;
				if( Debugger.IsAttached ) Debugger.Break( );
			}
		}


		[System.Diagnostics.CodeAnalysis.SuppressMessage( "Design", "CA1031:Do not catch general exception types", Justification = "<Pending>" )]
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
				if( Debugger.IsAttached ) Debugger.Break( );
			}
		}
	}
}
