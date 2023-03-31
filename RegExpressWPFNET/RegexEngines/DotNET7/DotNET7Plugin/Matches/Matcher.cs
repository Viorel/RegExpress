using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using RegExpressLibrary;
using RegExpressLibrary.Matches;


namespace DotNET7Plugin.Matches
{
	class Matcher : IMatcher
	{
		readonly Regex mRegex;

		public Matcher( Regex regex )
		{
			mRegex = regex;
		}


		#region IMatcher

		public RegexMatches Matches( string text, ICancellable cnc )
		{
			bool cancelled = false;
			Exception exception = null;
			ReMatch[] matches = null;

			var thread = new Thread( ( ) =>
			{
				try
				{
					var dotnet_matches = mRegex.Matches( text ); // no timeouts

					matches = // must do here to achieve the timeouts
						dotnet_matches
							.OfType<Match>( )
							.TakeWhile( m => !cnc.IsCancellationRequested )
							.Select( m => new ReMatch( m ) )
							.ToArray( );
				}
				catch( ThreadInterruptedException )
				{
					cancelled = true;
				}
				catch( ThreadAbortException )
				{
					cancelled = true;
				}
				catch( Exception exc )
				{
					exception = exc;
				}
			} )
			{
				IsBackground = true
			};

			thread.Start( );

			for(; ; )
			{
				if( thread.Join( 222 ) ) break;

				if( cnc.IsCancellationRequested )
				{
					thread.Interrupt( );
					// NOT SUPPORTED: if( !thread.Join( 1 ) ) thread.Abort( );
					thread.Join( 1 );

					break;
				}
			}

			if( exception != null ) throw exception;

			if( cancelled || cnc.IsCancellationRequested ) return RegexMatches.Empty;

			return new RegexMatches( matches.Length, matches );
		}

		#endregion IMatcher
	}
}
