import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;


class ReggieWorker
{
    public static void main( String[] args) 
    {
        boolean is_debug = false;

        try 
        {
            // disable logging
            InputStream stream = new ByteArrayInputStream("java.util.logging.ConsoleHandler.level=OFF".getBytes(StandardCharsets.UTF_8));
            java.util.logging.LogManager.getLogManager().readConfiguration(stream);

            byte[] input_bytes = System.in.readAllBytes();
            String input = new String( input_bytes, StandardCharsets.UTF_8);

            JSONParser parser = new JSONParser();
            JSONObject input_json = (JSONObject)parser.parse( input); // TODO: use reader

            String input_pattern = (String)input_json.get( "pattern");
            String input_text = (String)input_json.get( "text");
            JSONObject input_options = (JSONObject)input_json.get( "options");
            
            is_debug =  GetBoolean( input_options, "debug");

            int flags = com.datadoghq.reggie.ReggieFlags.NONE;
            if( GetBoolean( input_options, "CASE_INSENSITIVE")) flags |= com.datadoghq.reggie.ReggieFlags.CASE_INSENSITIVE;
            if( GetBoolean( input_options, "MULTILINE")) flags |= com.datadoghq.reggie.ReggieFlags.MULTILINE;
            if( GetBoolean( input_options, "DOTALL")) flags |= com.datadoghq.reggie.ReggieFlags.DOTALL;
            if( GetBoolean( input_options, "LITERAL")) flags |= com.datadoghq.reggie.ReggieFlags.LITERAL;
            if( GetBoolean( input_options, "UNICODE_CHARACTER_CLASS")) flags |= com.datadoghq.reggie.ReggieFlags.UNICODE_CHARACTER_CLASS;

            com.datadoghq.reggie.ReggieOptions.Builder option_builder = com.datadoghq.reggie.ReggieOptions.builder();
            if( GetBoolean( input_options, "CAPTURE_NAMED_ONLY")) option_builder = option_builder.namedOnly();
            if( GetBoolean( input_options, "ALLOW_JDK_FALLBACK")) option_builder = option_builder.allowJdkFallback();
            com.datadoghq.reggie.ReggieOptions options = option_builder.build();

            com.datadoghq.reggie.runtime.ReggieMatcher matcher = com.datadoghq.reggie.Reggie.compile( input_pattern, flags, options);
            List<com.datadoghq.reggie.runtime.MatchResult> matches = matcher.findAll( input_text);

            Set<String> possible_names = new TreeSet<String>();
            {
                java.util.regex.Matcher m = java.util.regex.Pattern.compile( "\\(\\s*\\?<\\s*([a-z][a-z0-9\\s]*)>", java.util.regex.Pattern.CASE_INSENSITIVE).matcher( input_pattern);
        
                while( m.find()) 
                {
                    String possible_name = m.group( 1);
                    possible_name = possible_name.replaceAll( "\\s+", "");
                    possible_names.add( possible_name);
                }
            }

            ArrayList<Object> all_matches = new ArrayList<>();

            for(int j = 0; j < matches.size(); ++j)
            {
                com.datadoghq.reggie.runtime.MatchResult match = matches.get(j);

                HashMap<String, Object> one_match = new HashMap<>();

                one_match.put("s", match.start());
                one_match.put("e", match.end());

                ArrayList<ArrayList<Number>> unnamed_groups = new ArrayList<>();

                for( int i = 0; i <= match.groupCount(); ++i)
                {
                    ArrayList<Number> a = new ArrayList<>();

                    a.add( match.start( i));
                    a.add( match.end( i));

                    unnamed_groups.add( a);
                }

                one_match.put( "g", unnamed_groups);

                ArrayList<Object> named_groups = new ArrayList<>();

                for( String name : possible_names)
                {
                    try
                    {
                        HashMap<String, Object> one_named_group = new HashMap<>();

                        one_named_group.put( "s", match.start( name));
                        one_named_group.put( "e", match.end( name));
                        one_named_group.put( "n", name);

                        named_groups.add( one_named_group);
                    }
                    catch( IllegalArgumentException exc)
                    {
                        // group name not found; ignore
                    }

                }

                one_match.put( "ng", named_groups);

                all_matches.add( one_match);
            }

            HashMap<String, Object> result = new HashMap<>();

            result.put( "matches", all_matches);

            String json = JSONObject.toJSONString( result);

            OutLn( json);

            System.exit( 0);
            return;
        } 
        catch( Exception e) 
        {
            if( is_debug)
            {
                e.printStackTrace();
            }
            else
            {
                ErrLn( e.toString());
            }
        }
    }

    static Boolean GetBoolean( JSONObject j, String k)
    {
        return j != null && j.containsKey( k) && (Boolean)j.get( k);
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
