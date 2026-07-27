using System.Diagnostics;
using System.Reflection;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text;
using go;

namespace GoRegexpWorker
{
    internal class Program
    {
        sealed class InputArgs
        {
            public required string pattern { get; set; }
            public required string text { get; set; }
            public Options? options { get; set; }
        }

        sealed class Options
        {
            public bool posix { get; set; }
            public bool longest { get; set; }
            public bool literal { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                GetMatches( input_args );
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
        }

        private static void GetMatches( InputArgs inputArgs )
        {
            string pattern;

            if( inputArgs.options?.literal == true )
            {
                pattern = go.regexp_package.QuoteMeta( inputArgs.pattern );
            }
            else
            {
                pattern = inputArgs.pattern ?? "";
            }

            (ж<regexp_package.Regexp> re, error err) =
                inputArgs.options?.posix == true ?
                    go.regexp_package.CompilePOSIX( pattern )
                    :
                    go.regexp_package.Compile( pattern );

            if( err != null ) throw new Exception( err.Error( ) );

            if( inputArgs.options?.longest == true ) re.Longest( );

            slice<@string> go_names = re.SubexpNames( );

            slice<slice<nint>> go_matches = re.FindAllStringSubmatchIndex( inputArgs.text ?? "", -1 );

            List<string> names = [];

            foreach( (nint _, @string n) in go_names )
            {
                names.Add( n );
            }

            List<List<int>> matches = [];

            foreach( (nint _, slice<nint> m) in go_matches )
            {
                List<int> one_match = [];

                foreach( (nint _, nint index) in m )
                {
                    one_match.Add( (int)index );
                }

                matches.Add( one_match );
            }

            var output_obj = new
            {
                Names = names,
                Matches = matches,
            };

            string ret_json = JsonSerializer.Serialize( output_obj );

            Console.Out.WriteLine( ret_json );
        }
    }
}
