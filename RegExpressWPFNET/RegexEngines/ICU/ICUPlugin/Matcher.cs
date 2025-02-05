using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Interop;
using RegExpressLibrary;
using RegExpressLibrary.Matches;
using RegExpressLibrary.Matches.Simple;


namespace ICUPlugin
{
    static class Matcher
    {
        public static RegexMatches GetMatches( ICancellable cnc, string pattern, string text, Options options )
        {
            Int32 limit = Int32.MaxValue;

            if( !string.IsNullOrWhiteSpace( options.Limit ) && !int.TryParse( options.Limit, out limit ) )
            {
                throw new ApplicationException( "Invalid limit. Please enter an integer number." );
            }

            uint flags = 0;
            //if(options.UREGEX_CANON_EQ) flags |= 1 << 0; // not implemented by ICU
            if( options.UREGEX_CASE_INSENSITIVE ) flags |= 1 << 1;
            if( options.UREGEX_COMMENTS ) flags |= 1 << 2;
            if( options.UREGEX_DOTALL ) flags |= 1 << 3;
            if( options.UREGEX_LITERAL ) flags |= 1 << 4;
            if( options.UREGEX_MULTILINE ) flags |= 1 << 5;
            if( options.UREGEX_UNIX_LINES ) flags |= 1 << 6;
            if( options.UREGEX_UWORD ) flags |= 1 << 7;
            if( options.UREGEX_ERROR_ON_UNKNOWN_ESCAPES ) flags |= 1 << 8;

            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "m" );
                bw.Write( (byte)'b' );

                bw.Write( pattern );
                bw.Write( text );
                bw.Write( flags );
                bw.Write( limit );

                bw.Write( (byte)'e' );
            };

            if( !ph.Start( cnc ) ) return RegexMatches.Empty;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            var br = ph.BinaryReader;

            if( br.ReadByte( ) != 'b' ) throw new Exception( "Invalid response B." );

            // read group names

            var group_names = new Dictionary<int, string>( );

            for(; ; )
            {
                int i = br.ReadInt32( );
                if( i <= 0 ) break;

                string name = br.ReadString( );

                group_names.Add( i, name );
            }

            // read matches

            List<IMatch> matches = new( );
            SimpleTextGetter? stg = null;

            for(; ; )
            {
                int group_count = br.ReadInt32( );
                if( group_count < 0 ) break;

                SimpleMatch? match = null; ;

                for( int i = 0; i <= group_count; ++i )
                {
                    int start = br.ReadInt32( );
                    bool success = start >= 0;
                    int end;
                    int length;
                    if( success )
                    {
                        end = br.ReadInt32( );
                        length = success ? end - start : 0;
                    }
                    else
                    {
                        end = 0;
                        length = 0;
                    }

                    if( i == 0 )
                    {
                        Debug.Assert( success );
                        Debug.Assert( match == null );

                        stg ??= new SimpleTextGetter( text );

                        match = SimpleMatch.Create( start, length, stg );
                        match.AddGroup( start, length, success, "0" );
                    }
                    else
                    {
                        string? name;

                        if( !group_names.TryGetValue( i, out name ) )
                        {
                            name = i.ToString( CultureInfo.InvariantCulture );
                        }

                        Debug.Assert( match != null );

                        match!.AddGroup( start, length, success, name );
                    }
                }

                Debug.Assert( match != null );

                matches.Add( match );
            }

            if( br.ReadByte( ) != 'e' ) throw new Exception( "Invalid response E." );

            return new RegexMatches( matches.Count, matches );
        }


        public static string? GetVersion( ICancellable cnc )
        {
            using ProcessHelper ph = new ProcessHelper( GetWorkerExePath( ) );

            ph.AllEncoding = EncodingEnum.Unicode;

            ph.BinaryWriter = bw =>
            {
                bw.Write( "v" );
            };

            if( !ph.Start( cnc ) ) return null;

            if( !string.IsNullOrWhiteSpace( ph.Error ) ) throw new Exception( ph.Error );

            string version_s = ph.BinaryReader.ReadString( );

            return version_s;
        }


        static string GetWorkerExePath( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;
            string worker_exe = Path.Combine( assembly_dir, @"ICUWorker.bin" );

            return worker_exe;
        }

    }
}
