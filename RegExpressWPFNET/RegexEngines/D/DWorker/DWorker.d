import std.compiler;
import std.format;
import std.stdio;
import std.json;
import std.regex;
import std.range;


void main()
{
    try
    {
        debug 
        { 
            const bool is_debug = true; 
        }
        else
        {
            const bool is_debug = false;
        }

        string s;
        stdin.readf( "%s", s);

        JSONValue json_value = parseJSON( s);

        const(JSONValue*) command_j = "c" in json_value;
        const string command = command_j == null ? "" : command_j.str;

        if( command == "v")
        {
            string v = format( "%s.%03s", version_major, version_minor);

            JSONValue result = JSONValue( ["version" : v]);

            writeln( result.toString());

            return;
        }

        if( command == "m" || command == "")
        {
            const string pattern = json_value["p"].str;
            const string text = json_value["t"].str;
            const(JSONValue*) flags_j = "f" in json_value;
            const string flags = flags_j == null ? "" : flags_j.str;

            auto re = regex( pattern, flags);

            JSONValue[] names;

            foreach( name; re.namedCaptures)
            {
                names ~= JSONValue( name);
            }

            JSONValue[] matches;

            foreach(match; std.regex.matchAll( text, re))
            {
                // ('match' is 'std.regex.Captures')

                JSONValue[] groups;

                groups ~= JSONValue( match.hit.ptr - text.ptr);
                groups ~= JSONValue( match.hit.length);

                foreach( capture; match) // "groups"
                {
                    // ( 'capture' is 'string')

                    if( capture == null) // failed (also if match is empty)
                    {
                        groups ~= JSONValue( -1);
                        groups ~= JSONValue( -1);
                    }
                    else
                    {
                        // See: https://forum.dlang.org/post/xdvjbcgvnnoxbryekawn@forum.dlang.org

                        groups ~= JSONValue( capture.ptr - text.ptr);
                        groups ~= JSONValue( capture.length);
                    }
                }

                JSONValue[] named_groups;

                foreach( name; re.namedCaptures)
                {
                    auto value = match[name];

                    if( value == null ) // failed (also if match is empty)
                    {
                        named_groups ~= JSONValue( -1);
                        named_groups ~= JSONValue( -1);
                    }
                    else
                    {
                        named_groups ~= JSONValue( value.ptr - text.ptr);
                        named_groups ~= JSONValue( value.length);
                    }
                }

                matches ~= JSONValue( 
                [
                    "g"  : groups,
                    "ng" : named_groups
                ]);
            }

            JSONValue result = [
                "names": names,
                "matches": matches
            ];

            writeln(toJSON( result, /*pretty:=*/ is_debug ));

            return;
        }

        stderr.writefln( "Unsupported command: '%s'", command);
    }
    catch (Exception exc)
    {
        stderr.write( typeid(exc).toString());
        stderr.write( ": ");
        stderr.writeln( exc.message());
    }
}
