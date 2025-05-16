import java.nio.charset.StandardCharsets;
import java.security.InvalidParameterException;
import java.util.Set;
import java.util.TreeSet;
import com.google.re2j.Pattern;
import com.google.re2j.Matcher;


class RE2JWorker
{
    public static void main( String[] args) 
    {
        try 
        {
            byte[] input_bytes = System.in.readAllBytes();
            String input = new String( input_bytes, StandardCharsets.UTF_8);

            // String input = 
            //     "get-matches" + "\u001F" + 
            //     "pattern" + "\u001F" + 
            //     "text" + "\u001F" + 
            //     "options + "\u001F" +
            //     "start + "\u001F" +
            //     "end + "\u001F" +
            //     "";

            String [ ] parts = input.split( "\u001F", -1 );
            String command = parts[0];

            switch( command.trim())
            {
            //case "get-version":
            //
            //    //System.getProperty("java.runtime.version") -- like "18.0.1.1+2-6"
            //    //System.getProperty("java.version") -- like "18.0.1.1"
            //    
            //    OutLn( "Version=" + System.getProperty("java.version"));
            //
            //    System.exit(0);
            //    return;

            case "get-matches":

                if( parts.length < 6)
                {
                    ErrLn( "No enought parameters: " + parts.length);

                    System.exit( 1);
                    return;
                }

                //
                // Unsupported features (vs. standard Java classes) are commented 
                //

                String input_pattern = parts[1];
                String input_text = parts[2];
                String input_options = parts[3];
                //String region_start_s = parts[4];
                //String region_end_s = parts[5];

                int options = 0;
                Boolean use_anchoring_bounds = false;
                Boolean use_transparent_bounds = false;

                //if( input_options.contains( ",CANON_EQ,")) options |= Pattern.CANON_EQ;
                if( input_options.contains( ",CASE_INSENSITIVE,")) options |= Pattern.CASE_INSENSITIVE;
                //if( input_options.contains( ",COMMENTS,")) options |= Pattern.COMMENTS;
                if( input_options.contains( ",DOTALL,")) options |= Pattern.DOTALL;
                //if( input_options.contains( ",LITERAL,")) options |= Pattern.LITERAL;
                if( input_options.contains( ",MULTILINE,")) options |= Pattern.MULTILINE;
                //if( input_options.contains( ",UNICODE_CASE,")) options |= Pattern.UNICODE_CASE;
                //if( input_options.contains( ",UNICODE_CHARACTER_CLASS,")) options |= Pattern.UNICODE_CHARACTER_CLASS;
                //if( input_options.contains( ",UNIX_LINES,")) options |= Pattern.UNIX_LINES;
                //if( input_options.contains( ",useAnchoringBounds,")) use_anchoring_bounds = true;
                //if( input_options.contains( ",useTransparentBounds,")) use_transparent_bounds = true;

                // re2j specific
                if( input_options.contains( ",DISABLE_UNICODE_GROUPS,")) options |= Pattern.DISABLE_UNICODE_GROUPS;
                if( input_options.contains( ",LONGEST_MATCH,")) options |= Pattern.LONGEST_MATCH;
                
                Pattern pattern = Pattern.compile( input_pattern, options);
                Matcher matcher = pattern.matcher( input_text);

                /*
                if( (region_start_s.trim().length() > 0) != (region_end_s.trim().length()) > 0 )
                {
                    throw new InvalidParameterException( "Both “start” and “end” must be entered or blank" );
                }

                if( region_start_s.trim().length() > 0 && region_end_s.trim().length() > 0 )
                {
                    int region_start = Integer.parseInt( region_start_s.trim() );
                    int region_end = Integer.parseInt( region_end_s.trim() );

                    matcher.region( region_start, region_end );
                }

                matcher.useAnchoringBounds( use_anchoring_bounds );
                matcher.useTransparentBounds( use_transparent_bounds );
                */

                Set<String> possible_names = new TreeSet<String>();
                {
                    java.util.regex.Matcher m = java.util.regex.Pattern.compile( "\\(\\s*\\?<\\s*([a-z][a-z0-9\\s]*)>", java.util.regex.Pattern.CASE_INSENSITIVE).matcher( input_pattern);
            
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
                            // group name not found; ignore
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
            ErrLn( e.getClass().getName() + ": " +  e.getMessage());
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
