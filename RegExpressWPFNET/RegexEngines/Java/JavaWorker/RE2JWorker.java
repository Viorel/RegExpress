import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Set;
import java.util.TreeSet;

import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;

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

            JSONParser parser = new JSONParser();
            JSONObject input_json = (JSONObject)parser.parse(input); // TODO: use reader

            String command = (String)input_json.get("command");

            switch( command.trim())
            {
            case "get-matches":

                String input_pattern = (String)input_json.get( "pattern");
                String input_text = (String)input_json.get( "text");
                JSONObject input_options = (JSONObject)input_json.get("options");

                int options = 0;
                if( GetBoolean( input_options, "CASE_INSENSITIVE")) options |= Pattern.CASE_INSENSITIVE;
                if( GetBoolean( input_options, "DOTALL")) options |= Pattern.DOTALL;
                if( GetBoolean( input_options, "MULTILINE")) options |= Pattern.MULTILINE;

                // re2j specific
                if( GetBoolean( input_options, "DISABLE_UNICODE_GROUPS")) options |= Pattern.DISABLE_UNICODE_GROUPS;
                if( GetBoolean( input_options, "LONGEST_MATCH")) options |= Pattern.LONGEST_MATCH;
                
                Pattern pattern = Pattern.compile( input_pattern, options);
                Matcher matcher = pattern.matcher( input_text);

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

                ArrayList<Object> all_matches = new ArrayList<>();

                while( matcher.find())
                {
                    HashMap<String, Object> one_match = new HashMap<>();

                    one_match.put("s", matcher.start());
                    one_match.put("e", matcher.end());

                    ArrayList<ArrayList<Number>> unnamed_groups = new ArrayList<>();

                    for( int i = 0; i <= matcher.groupCount(); ++i)
                    {
                        ArrayList<Number> a = new ArrayList<>();

                        a.add(matcher.start(i));
                        a.add(matcher.end(i));

                        unnamed_groups.add(a);
                    }

                    one_match.put("g", unnamed_groups);

                    ArrayList<Object> named_groups = new ArrayList<>();

                    for( String name : possible_names)
                    {
                        try
                        {
                            HashMap<String, Object> one_named_group = new HashMap<>();

                            one_named_group.put("s", matcher.start( name));
                            one_named_group.put("e", matcher.end( name));
                            one_named_group.put("n", name);

                            named_groups.add(one_named_group);
                        }
                        catch( IllegalArgumentException exc)
                        {
                            // group name not found; ignore
                        }
                    }

                    one_match.put("ng", named_groups);

                    all_matches.add(one_match);
                }

                HashMap<String, Object> result = new HashMap<>();

                result.put("matches", all_matches);

                String json = JSONObject.toJSONString(result);

                OutLn( json);

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


    static Boolean GetBoolean( JSONObject j, String k)
    {
        return j != null && j.containsKey( k) && (Boolean)j.get(k);
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
