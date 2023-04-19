using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressWPFNET.Code
{
	internal static class UnicodeUtilities
	{
		static readonly BitArray RorALarray = new( 0xFFFF + 1, false );


		static UnicodeUtilities( )
		{
			// Copied from "https://www.ietf.org/rfc/rfc3454.txt",
			// D.1 Characters with bidirectional property "R" or "AL"
			// See also: https://stackoverflow.com/questions/4330951/how-to-detect-whether-a-character-belongs-to-a-right-to-left-language

			const string RorALranges =
@"
   05BE
   05C0
   05C3
   05D0-05EA
   05F0-05F4
   061B
   061F
   0621-063A
   0640-064A
   066D-066F
   0671-06D5
   06DD
   06E5-06E6
   06FA-06FE
   0700-070D
   0710
   0712-072C
   0780-07A5
   07B1
   200F
   FB1D
   FB1F-FB28
   FB2A-FB36
   FB38-FB3C
   FB3E
   FB40-FB41
   FB43-FB44
   FB46-FBB1
   FBD3-FD3D
   FD50-FD8F
   FD92-FDC7
   FDF0-FDFC
   FE70-FE74
   FE76-FEFC
";

			foreach( Match m in Regex.Matches( RorALranges, @"(?'first'[0-9A-F]{4})(-(?'second'[0-9A-F]{4}))?" ) )
			{
				int start = Convert.ToInt32( m.Groups["first"].Value, 16 );
				var g = m.Groups["second"];
				int end = g.Success ? Convert.ToInt32( g.Value, 16 ) : start;

				for( int i = start; i <= end; ++i )
				{
					RorALarray[i] = true;
				}
			}
		}


		public static bool IsRTL( char c )
		{
			return RorALarray[c];
		}
	}
}
