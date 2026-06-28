import 'dart:io';
import 'dart:convert';


void main() async 
{
  try 
  {
    // Read all data from stdin as UTF-8 text
    final String input = await stdin.transform( utf8.decoder).join();

    //print( "Input: $input");

    final Map<String, dynamic> jsonMap = jsonDecode( input);
    final String pattern = jsonMap["pattern"];
    final String text = jsonMap["text"];
    final Map<String, dynamic>? options = jsonMap["options"];
    final bool is_multiline = options?["multiline"] == true;
    final bool is_case_sensitive = options?["caseSensitive"] == true;
    final bool is_unicode = options?["unicode"] == true;
    final bool is_dot_all = options?["dotAll"] == true;

    //print( "Pattern: '$pattern'");
    //print( "Text: '$text'");

    final RegExp re = new RegExp( pattern, multiLine: is_multiline, caseSensitive: is_case_sensitive, unicode: is_unicode, dotAll: is_dot_all);

    final Iterable<RegExpMatch> matches = re.allMatches(text);

    final List<dynamic> output_matches = [];

    for( final m in matches) 
    {
      final Map<String, dynamic> one_match = new Map<String, dynamic>();
      one_match["s"] = m.start;
      one_match["e"] = m.end;

      final List<String?> all_groups = [ ];

      for( int i = 1; i <= m.groupCount; ++i)
      {
        all_groups.add( m.group(i));
      }

      one_match["g"] = all_groups;

      final List<Map<String, dynamic>> all_named_groups = [ ];

      for( final name in m.groupNames)
      {
        final Map<String, dynamic> one_named_group = new Map<String, dynamic>();
        one_named_group["n"] = name;
        one_named_group["v"] = m.namedGroup(name);

        all_named_groups.add( one_named_group);
      }

      one_match["ng"] = all_named_groups;

      output_matches.add(one_match);
    }

    final Map<String, dynamic> output_object = new Map<String, dynamic>();

    output_object["Matches"] = output_matches;

    final String output_json = jsonEncode( output_object);

    stdout.writeln( output_json);

    exit( 0);
  } 
  catch (e) 
  {
    stderr.writeln( "Error: $e");

    exit( 1);
  }
}
