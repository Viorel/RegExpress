import java.nio.charset.StandardCharsets;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.regex.Matcher;


class JavaWorker
{
    public static void main( String[] args) 
    {
        try 
        {
            //String input = "get-version";
            //String input = "get-matches\u001F(?<n1>a)(b)?(c)\u001Fac\u001F-";

            byte[] input_bytes = System.in.readAllBytes();
            String input = new String( input_bytes, StandardCharsets.UTF_8);

            // String input = 
            //     "get-matches" + "\u001F" + 
            //     "(ș" + "\u001F" + 
            //     "ș" + "\u001F" + 
            //     "-";

            String [ ] parts = input.split( "\u001F");
            String command = parts[0];

            switch( command.trim())
            {
            case "get-version":

                //System.getProperty("java.runtime.version") -- like "18.0.1.1+2-6"
                //System.getProperty("java.version") -- like "18.0.1.1"
                
                OutLn( "Version=" + System.getProperty("java.version"));

                System.exit(0);
                return;

            case "get-matches":

                if( parts.length < 4)
                {
                    ErrLn( "No enought parameters: " + parts.length);

                    System.exit( 1);
                    return;
                }

                String input_pattern = parts[1];
                String input_text = parts[2];
                String input_options = parts[3];

                int options = 0;

                if( input_options.contains( ",CANON_EQ,")) options |= Pattern.CANON_EQ;
                if( input_options.contains( ",CASE_INSENSITIVE,")) options |= Pattern.CASE_INSENSITIVE;
                if( input_options.contains( ",COMMENTS,")) options |= Pattern.COMMENTS;
                if( input_options.contains( ",DOTALL,")) options |= Pattern.DOTALL;
                if( input_options.contains( ",LITERAL,")) options |= Pattern.LITERAL;
                if( input_options.contains( ",MULTILINE,")) options |= Pattern.MULTILINE;
                if( input_options.contains( ",UNICODE_CASE,")) options |= Pattern.UNICODE_CASE;
                if( input_options.contains( ",UNICODE_CHARACTER_CLASS,")) options |= Pattern.UNICODE_CHARACTER_CLASS;
                if( input_options.contains( ",UNIX_LINES,")) options |= Pattern.UNIX_LINES;
                
                Pattern pattern = Pattern.compile( input_pattern, options);
                Matcher matcher = pattern.matcher( input_text);


                Set<String> possible_names = new TreeSet<String>();
                {
                    Matcher m = Pattern.compile( "\\(\\s*\\?<\\s*([a-z][a-z0-9\\s]*)>", Pattern.CASE_INSENSITIVE).matcher( input_pattern);
            
                    while( m.find()) 
                    {
                        String possible_name = m.group(1);
                        possible_name = possible_name.replaceAll( "\\s+", "");
                        possible_names.add( possible_name);
                    }
                }

                //OutLn("D pattern: '" + input_pattern + "'");
                //OutLn("D text: '" + input_text + "'");

                while( matcher.find())
                {
                    OutLn( "M " + matcher.start() + " " + matcher.end());

                    for( int i = 0; i <= matcher.groupCount(); ++i)
                    {
                        OutLn( "G " + matcher.start(i) + " " + matcher.end(i));
                    }

                    for( String name : possible_names)
                    {
                        String value = null;
                        try
                        {
                            value = matcher.group( name);
                        }
                        catch( IllegalArgumentException exc)
                        {
                            // group name not vound; ignore
                        }
                        if( value != null)
                        {
                            OutLn( "N " + matcher.start( name) + " " + matcher.end( name) + " <" + name + ">");
                        }
                    }
                }

                System.exit( 0);
                return;

            default:

                ErrLn( "Unknown command: '" + command + "'");
                System.exit( 1);
                return;
            }

        } 
        catch( Exception e) 
        {
            //e.printStackTrace();
            ErrLn( e.getLocalizedMessage());
        }
    }


    static void OutLn( String text)
    {
        System.out.writeBytes( text.getBytes( StandardCharsets.UTF_8));
        System.out.writeBytes( "\r\n".getBytes( StandardCharsets.UTF_8));
    }


    static void ErrLn( String text)
    {
        System.err.writeBytes( text.getBytes( StandardCharsets.UTF_8));
        System.err.writeBytes( "\r\n".getBytes( StandardCharsets.UTF_8));
    }
}
