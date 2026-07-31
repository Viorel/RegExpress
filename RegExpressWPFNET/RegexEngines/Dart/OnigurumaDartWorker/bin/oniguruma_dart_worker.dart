// ignore_for_file: non_constant_identifier_names

import 'dart:io';
import 'dart:convert';

import 'package:oniguruma_dart/oniguruma_dart.dart';

void main() async 
{
  try 
  {
    final String input = await stdin.transform( utf8.decoder).join();

    //print( "Input: $input");

    final Map<String, dynamic> jsonMap = jsonDecode( input);
    final String syntax = jsonMap["syntax"] ?? "";
    final String pattern = jsonMap["pattern"];
    final String text = jsonMap["text"];
    final Map<String, dynamic>? options = jsonMap["options"];

    final List<String> possible_names = [];

    for( final OnigMatch m in OnigRegex.compile("<(\\w+?)>|'(\\w+?)'").allMatches(pattern))
    {
      final String? n = m.group(1) ?? m.group(2); 
      if( n != null && !possible_names.contains(n) )
      {
        possible_names.add(n);
      }
    }

    //print( "Pattern: '$pattern'");
    //print( "Text: '$text'");

    final OnigRegex re = OnigRegex.compile( pattern, 
      syntax: switch (syntax)
      {
        "" || "onigSyntaxOniguruma" => onigSyntaxOniguruma,
        "onigSyntaxRuby"            => onigSyntaxRuby,         
        "onigSyntaxPerl"            => onigSyntaxPerl,         
        "onigSyntaxPerlNg"          => onigSyntaxPerlNg,       
        "onigSyntaxJava"            => onigSyntaxJava,         
        "onigSyntaxPython"          => onigSyntaxPython,       
        "onigSyntaxGrep"            => onigSyntaxGrep,         
        "onigSyntaxEmacs"           => onigSyntaxEmacs,        
        "onigSyntaxPosixBasic"      => onigSyntaxPosixBasic,   
        "onigSyntaxPosixExtended"   => onigSyntaxPosixExtended,
        "onigSyntaxGnuRegex"        => onigSyntaxGnuRegex,     
        String() => throw ArgumentError("Invalid syntax: '$syntax'"),
      },
      options: 
        (options?["ignoreCase"] == true ? OnigOption.ignoreCase : 0) |
        (options?["extend"] == true ? OnigOption.extend : 0) |
        (options?["multiLine"] == true ? OnigOption.multiLine : 0) |
        (options?["singleLine"] == true ? OnigOption.singleLine : 0) |
        (options?["findLongest"] == true ? OnigOption.findLongest : 0) |
        (options?["findNotEmpty"] == true ? OnigOption.findNotEmpty : 0) |
        (options?["negateSingleLine"] == true ? OnigOption.negateSingleLine : 0) |
        (options?["dontCaptureGroup"] == true ? OnigOption.dontCaptureGroup : 0) |
        (options?["captureGroup"] == true ? OnigOption.captureGroup : 0) |
        (options?["notBol"] == true ? OnigOption.notBol : 0) |
        (options?["notEol"] == true ? OnigOption.notEol : 0) |
        (options?["posixRegion"] == true ? OnigOption.posixRegion : 0) |
        (options?["checkValidityOfString"] == true ? OnigOption.checkValidityOfString : 0) |
        (options?["ignoreCaseIsAscii"] == true ? OnigOption.ignoreCaseIsAscii : 0) |
        (options?["wordIsAscii"] == true ? OnigOption.wordIsAscii : 0) |
        (options?["digitIsAscii"] == true ? OnigOption.digitIsAscii : 0) |
        (options?["spaceIsAscii"] == true ? OnigOption.spaceIsAscii : 0) |
        (options?["posixIsAscii"] == true ? OnigOption.posixIsAscii : 0) |
        (options?["textSegmentExtendedGraphemeCluster"] == true ? OnigOption.textSegmentExtendedGraphemeCluster : 0) |
        (options?["textSegmentWord"] == true ? OnigOption.textSegmentWord : 0) |
        (options?["notBeginString"] == true ? OnigOption.notBeginString : 0) |
        (options?["notEndString"] == true ? OnigOption.notEndString : 0) |
        (options?["notBeginPosition"] == true ? OnigOption.notBeginPosition : 0) |
        (options?["callbackEachMatch"] == true ? OnigOption.callbackEachMatch : 0) |
        (options?["matchWholeString"] == true ? OnigOption.matchWholeString : 0) |
        0
       );

    final Iterable<OnigMatch> all_matches = re.allMatches(text);

    final List<dynamic> output_matches = [];

    for( final OnigMatch m in all_matches) 
    {
      final Map<String, dynamic> one_match = <String, dynamic>{};
      one_match["s"] = m.start;
      one_match["e"] = m.end;

      final List<dynamic> all_groups = [ ];

      for( int i = 1; i <= m.groupCount; ++i)
      {
        final Map<String, dynamic> one_group = <String, dynamic>{};
        one_group["s"] = m.startOf(i);
        one_group["e"] = m.endOf(i);

        // TODO: get name
        //m.

        all_groups.add( one_group);
      }

      one_match["g"] = all_groups;

      final List<Map<String, String>> named_values = [];

      for( final String possible_name in possible_names)
      {
        final String? value = m.namedGroup(possible_name);

        if( value != null)
        {
          final Map<String, String> p = <String, String>{};

          p["n"] = possible_name;
          p["v"] = value;

          named_values.add(p);
        }
      }

      one_match["nv"] = named_values;

      output_matches.add(one_match);
    }

    final Map<String, dynamic> output_object = <String, dynamic>{};
    output_object["Matches"] = output_matches;
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
