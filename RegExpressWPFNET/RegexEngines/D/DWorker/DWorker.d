import std.compiler;
import std.format;
import std.stdio;
import std.json;
import std.regex;
import std.range;
//import std.algorithm;


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
		stdin.readf("%s", s);

		JSONValue json_value = parseJSON(s);

		const(JSONValue*) command_j = "c" in json_value;
		const string command = command_j == null ? "" : command_j.str;

		if( command == "v")
		{
			string v = format("%s.%03s", version_major, version_minor);

			JSONValue result = JSONValue(["version" : v]);

			writeln(result.toString());

			return;
		}

		if( command == "m" || command == "")
		{
			const string pattern = json_value["p"].str;
			const string text = json_value["t"].str;
			const(JSONValue*) flags_j = "f" in json_value;
			const string flags = flags_j == null ? "" : flags_j.str;

			auto re = regex(pattern, flags);

			JSONValue[] names;

			foreach(name; re.namedCaptures)
			{
				names ~= JSONValue(name);
			}

			JSONValue[] matches;
			JSONValue[] empty_array0;
			JSONValue empty_array = JSONValue(empty_array0);

			foreach(match; std.regex.matchAll(text, re))
			{
				// ('match' is 'std.regex.Captures')

				if( match.empty) continue;

				JSONValue[] groups;

				foreach( capture; match) // "groups"
				{
					// ( 'capture' is 'string')

					if( capture == null) // failed?
					{
						groups ~= empty_array;
					}
					else
					{
						// See: https://forum.dlang.org/post/xdvjbcgvnnoxbryekawn@forum.dlang.org

						groups ~= JSONValue([capture.ptr - text.ptr, capture.length]);
					}
				}

				JSONValue[] named_groups;

				foreach(name; re.namedCaptures)
				{
					auto val = match[name];

					if( val.empty) 
					{
						named_groups ~= JSONValue([ -1, 0 ]);
					}
					else
					{
						named_groups ~= JSONValue([ val.ptr - text.ptr, val.length ]);
					}
				}

				auto const i = match.hit.ptr - text.ptr;

				JSONValue one = [ 
					"i": JSONValue(i),
					"g": JSONValue(groups),
					"n": JSONValue(named_groups)
				];

				matches ~= one;
			}

			JSONValue result = [
				"names": names,
				"matches": matches
			];

			writeln(toJSON(result, /*pretty:=*/ is_debug ));

			return;
		}

		stderr.writefln("Unsupported command: '%s'", command);
	}
	catch (RegexException exc)
	{
		stderr.writeln(exc.message());
	}
	catch (Exception exc)
	{
		stderr.writeln(exc);
	}
}
