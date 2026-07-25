import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Pattern;

import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;

import java.util.regex.Matcher;
import com.datadoghq.reggie.Reggie;
import com.datadoghq.reggie.runtime.MatchResult;
import com.datadoghq.reggie.runtime.ReggieMatcher;

class ReggieWorker
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
                
                ReggieMatcher matcher = Reggie.compile( input_pattern);
                List<MatchResult> matches = matcher.findAll(input_text);

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

                ArrayList<Object> all_matches = new ArrayList<>();

                for(int j = 0; j < matches.size(); ++j)
                {
                    MatchResult match = matches.get(j);

                    HashMap<String, Object> one_match = new HashMap<>();

                    one_match.put("s", match.start());
                    one_match.put("e", match.end());

                    ArrayList<ArrayList<Number>> unnamed_groups = new ArrayList<>();

                    for( int i = 0; i <= match.groupCount(); ++i)
                    {
                        ArrayList<Number> a = new ArrayList<>();

                        a.add(match.start(i));
                        a.add(match.end(i));

                        unnamed_groups.add(a);
                    }

                    one_match.put("g", unnamed_groups);

                    ArrayList<Object> named_groups = new ArrayList<>();

                    for( String name : possible_names)
                    {
                        try
                        {
                            HashMap<String, Object> one_named_group = new HashMap<>();

                            one_named_group.put("s", match.start( name));
                            one_named_group.put("e", match.end( name));
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
