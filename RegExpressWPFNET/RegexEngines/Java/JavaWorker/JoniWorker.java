import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;
import org.jcodings.specific.UTF16BEEncoding;
import org.jcodings.specific.UTF16LEEncoding;
import org.jcodings.specific.UTF32LEEncoding;
import org.jcodings.specific.UTF8Encoding;

class JoniWorker
{
    public static void main( String[] args) 
    {
        try 
        {
            byte[] input_bytes = System.in.readAllBytes();
            String input = new String( input_bytes, StandardCharsets.UTF_8);

            JSONParser parser = new JSONParser();
            JSONObject input_json = (JSONObject)parser.parse( input); // TODO: use reader

            String input_pattern = (String)input_json.get( "pattern");
            String input_text = (String)input_json.get( "text");
            JSONObject input_options = (JSONObject)input_json.get( "options");

            int options = org.joni.Option.NONE;
            if( GetBoolean( input_options, "IGNORECASE")) options |= org.joni.Option.IGNORECASE;
            if( GetBoolean( input_options, "EXTEND")) options |= org.joni.Option.EXTEND;
            if( GetBoolean( input_options, "MULTILINE")) options |= org.joni.Option.MULTILINE;
            if( GetBoolean( input_options, "SINGLELINE")) options |= org.joni.Option.SINGLELINE;
            if( GetBoolean( input_options, "FIND_LONGEST")) options |= org.joni.Option.FIND_LONGEST;
            if( GetBoolean( input_options, "FIND_NOT_EMPTY")) options |= org.joni.Option.FIND_NOT_EMPTY;
            if( GetBoolean( input_options, "NEGATE_SINGLELINE")) options |= org.joni.Option.NEGATE_SINGLELINE;
            if( GetBoolean( input_options, "DONT_CAPTURE_GROUP")) options |= org.joni.Option.DONT_CAPTURE_GROUP;
            if( GetBoolean( input_options, "CAPTURE_GROUP")) options |= org.joni.Option.CAPTURE_GROUP;

            if( GetBoolean( input_options, "NOTBOL")) options |= org.joni.Option.NOTBOL;
            if( GetBoolean( input_options, "NOTEOL")) options |= org.joni.Option.NOTEOL;
            if( GetBoolean( input_options, "NEWLINE_CRLF")) options |= org.joni.Option.NEWLINE_CRLF;
            if( GetBoolean( input_options, "NOTBOS")) options |= org.joni.Option.NOTBOS;
            if( GetBoolean( input_options, "NOTEOS")) options |= org.joni.Option.NOTEOS;
            if( GetBoolean( input_options, "ASCII_RANGE")) options |= org.joni.Option.ASCII_RANGE;
            if( GetBoolean( input_options, "POSIX_BRACKET_ALL_RANGE")) options |= org.joni.Option.POSIX_BRACKET_ALL_RANGE;
            if( GetBoolean( input_options, "WORD_BOUND_ALL_RANGE")) options |= org.joni.Option.WORD_BOUND_ALL_RANGE;
            if( GetBoolean( input_options, "CR_7_BIT")) options |= org.joni.Option.CR_7_BIT;

            byte[] pattern_bytes = input_pattern.getBytes( StandardCharsets.UTF_8);
            byte[] text_bytes = input_text.getBytes( StandardCharsets.UTF_8);

            org.joni.Regex regex = new org.joni.Regex( pattern_bytes, 0, pattern_bytes.length, options, UTF8Encoding.INSTANCE);
            org.joni.Matcher matcher = regex.matcher( text_bytes);
            
            int start = 0;

            ArrayList<Object> all_matches = new ArrayList<>();

            for(;;)
            {
                int result = matcher.search( start, text_bytes.length, options);

                if( result < 0) break;
            
                org.joni.Region region = matcher.getEagerRegion();

                // unnamed group, including main one

                ArrayList<Object> unnamed_groups = new ArrayList<>();

                for( int i = 0; i < region.getNumRegs(); ++i)
                {
                    int begin = region.getBeg( i);
                    int end = region.getEnd( i);

                    ArrayList<Number> g = new ArrayList<>();
                    g.add( begin);
                    g.add( end);

                    unnamed_groups.add( g);
                }

                // named group

                ArrayList<Object> named_groups = new ArrayList<>();

                for (Iterator<org.joni.NameEntry> entry = regex.namedBackrefIterator(); entry.hasNext();) 
                {
                    org.joni.NameEntry e = entry.next();

                    int[] br = e.getBackRefs();

                    for(int j = 0; j < br.length; ++j)
                    {
                        int number = br[j]; // can have many refs per name

                        int begin = region.getBeg( number);
                        int end = region.getEnd( number);
                        String name = new String( e.name, e.nameP, e.nameEnd - e.nameP, StandardCharsets.UTF_8);

                        HashMap<String, Object> ng = new HashMap<>();
                        ng.put( "n", name);
                        ng.put( "s", begin);
                        ng.put( "e", end);

                        named_groups.add( ng);
                    }
                }

                HashMap<String, Object> one_match = new HashMap<>();

                one_match.put( "g", unnamed_groups);
                one_match.put( "ng", named_groups);

                all_matches.add( one_match);

                int new_start = region.getEnd(0);

                if( new_start == start)
                {
                    ++start;
                }
                else
                {
                    start = new_start;
                }
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
            //e.printStackTrace();
            ErrLn( e.getClass().getName() + ": " +  e.getMessage());
        }
    }

    static Boolean GetBoolean( JSONObject j, String k)
    {
        return j != null && j.containsKey( k) && (Boolean)j.get( k);
    }

    static Integer GetInteger( JSONObject j, String k)
    {
        if( j == null) return null;
        
        Long l = (Long)j.get( k);

        return l == null ? null : l.intValue();
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
