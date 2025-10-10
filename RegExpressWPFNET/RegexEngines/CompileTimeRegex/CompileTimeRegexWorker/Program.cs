using System.Diagnostics;
using System.Reflection;
using System.Runtime.Versioning;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Text;


namespace CompileTimeRegexWorker
{
    internal class Program
    {
        sealed class InputArgs
        {
            public string? command { get; set; }
            public string? pattern { get; set; }
            public string? text { get; set; }
            public string? flags { get; set; }
        }


        static void Main( string[] args )
        {
            try
            {
                Console.InputEncoding = Encoding.UTF8;
                Console.OutputEncoding = Encoding.UTF8;

                string input_string = Console.In.ReadToEnd( );

                InputArgs input_args = JsonSerializer.Deserialize<InputArgs>( input_string )!;

                switch( input_args.command )
                {
                case "get-matches":
                    GetMatches( input_args );
                    break;
                default:
                    throw new ArgumentException( $"Command not supported: '{input_args.command}'" );
                }
            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
        }

        private static void GetMatches( InputArgs inputArgs )
        {
            string temp_path = Path.GetTempPath( );
            try
            {
                string worker_dir = GetWorkerDirectory( );



            }
            catch( Exception exc )
            {
                Console.Error.WriteLine( exc.Message );
            }
            finally
            {
                new DirectoryInfo( temp_path ).Delete( recursive: true );
            }
        }

        static string GetWorkerDirectory( )
        {
            string assembly_location = Assembly.GetExecutingAssembly( ).Location;
            string assembly_dir = Path.GetDirectoryName( assembly_location )!;

            return assembly_dir;
        }

    }
}