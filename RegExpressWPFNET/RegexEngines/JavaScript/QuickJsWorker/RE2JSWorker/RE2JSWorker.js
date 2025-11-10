import * as std from "std";
import { RE2JS } from './re2js/build/index.esm.js'

try
{
  const input_text = std.in.readAsString();
  const input_object = JSON.parse( input_text );

  const pattern = input_object.pattern;
  const text = input_object.text;
  const flags = "" + input_object.flags;

  let flags_number = 0;
  if( flags.includes("i")) flags_number |= RE2JS.CASE_INSENSITIVE;
  if( flags.includes("s")) flags_number |= RE2JS.DOTALL;
  if( flags.includes("m")) flags_number |= RE2JS.MULTILINE;
  if( flags.includes("U")) flags_number |= RE2JS.DISABLE_UNICODE_GROUPS;
  if( flags.includes("l")) flags_number |= RE2JS.LONGEST_MATCH;

  const compiled_pattern = RE2JS.compile( pattern, flags_number );
  const matcher = compiled_pattern.matcher( text );

  let result = [ ];

  while( matcher.find( )) 
  {
    let all_groups = [ ];

    all_groups.push( [ matcher.start(0), matcher.end(0) ] );

    for( let i = 1; i <= matcher.groupCount( ); ++i)
    {
      if( matcher.start(i) < 0)
      {
        all_groups.push( [ -1, -1] );
      }
      else
      {
        all_groups.push( [ matcher.start(i), matcher.end(i) ] );
      }    
    } 

    let named_groups = [ ];

    for( let n in compiled_pattern.namedGroups( ))
    {
      if( matcher.start(n) < 0)
      {
        named_groups.push( { n : n, s : -1, e : -1 }  );
      }
      else
      {
        named_groups.push( { n : n, s : matcher.start(n), e : matcher.end(n) } );
      }
    }

    result.push( { ag : all_groups, ng : named_groups } );
  }

  console.log( JSON.stringify( { Matches: result } ) );
}
catch( err )
{
  console.log( JSON.stringify( { Error: `${err.name}: ${err.message}` } ) ); 
} 
