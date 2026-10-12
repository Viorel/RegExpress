// ignore_for_file: non_constant_identifier_names

//import 'package:i_regexp_worker/i_regexp_worker.dart' as i_regexp_worker;

import 'dart:io';
import 'dart:convert';

import 'package:iregexp/iregexp.dart';

void main(List<String> arguments) async
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

    final matchesSubstring = options?["matchesSubstring"] == true;

    final iRegexp = IRegexp(pattern);
    final is_match = matchesSubstring ? iRegexp.matchesSubstring( text) : iRegexp.matches( text);

    final Map<String, dynamic> output_object = <String, dynamic>{};

    output_object["is_match"] = is_match;

    final String output_json = jsonEncode( output_object);

    stdout.writeln( output_json);

    exit( 0);
  } 
  catch (e) 
  {
    stderr.writeln( "$e");

    exit( 1);
  }
}
