import * as std from "std";

"use strict";

//import * as Regex from "regex/dist/esm/regex.js"; // Ok, using 'Regex.' prefix
//import * as Plus from 'regex/dist/regex.min.js'; // Errors

std.loadScript( 'regex-min/dist/regex.min.js' ); // ok, using 'Regex.' prefix

try
{
  const input_text = std.in.readAsString();
  const input_object = JSON.parse( input_text );

  const pattern = input_object.pattern;
  const text = input_object.text;
  const flags = "" + input_object.flags;
  const func = input_object.func;

  const x_flag = flags.includes("x");
  const n_flag = flags.includes("n");
  //const u_flag = flags.includes("u"); // no use
  const v_flag = flags.includes("v");
  const subclass = flags.includes("C");

  let adjusted_flags = flags.replace(/[vuxnC]/g, ""); // to avoid "Implicit flags v/u/x/n cannot be explicitly added", and to remove special "C"

  if( ! adjusted_flags.includes("d")) adjusted_flags += "d"; // "d" -- generate indices

  const options =
  {
    flags: adjusted_flags,
    subclass: subclass,
    disable: 
    {
      x: !x_flag,
      n: !n_flag,
      v: !v_flag, 
      atomic: false,
      subroutines: false,
    },
    force: 
    {
      //v: false,
    }
  };

  //const re = Regex.regex(options)`${Regex.pattern(pattern)}`; // "(?(DEFINE)" does not work; will use 'eval'
  
  const adjusted_pattern = pattern.replace(/`/g, "\\u0060");
  const re = eval(`Regex.regex(options)\`${adjusted_pattern}\``);
  
  if( func === "exec")
  {
    const matches = [ ]; 
    let match; 
    let lastIndex = -2;

    while( ( match = re.exec( text)) !== null)
    {
      if( lastIndex == match.index) throw new Error( "No advance");
      lastIndex = match.index;

      matches.push( { i: match.indices, g: match.indices.groups });

      if( ! re.global) break;

      if( re.lastIndex == match.index) 
      {
        if( text.codePointAt( match.index) >= 0xFFFF)
        {
            re.lastIndex += 2;
        }
        else
        {
            ++re.lastIndex;
        }
      }
    } 
    
    console.log( JSON.stringify( { Matches: matches } ) );
  }
  else if( func === "matchAll")
  {
    const matches = [ ];
    let lastIndex = -2;

    for( const match of text.matchAll( re))
    {
      if( lastIndex == match.index) throw new Error( "No advance");
      lastIndex = match.index;

      matches.push( { i: match.indices, g: match.indices.groups });
    }

    console.log( JSON.stringify( { Matches: matches } ) );
  }
  else
  {
    console.log( JSON.stringify( { Error: `Invalid 'func': "${func}"` } ) ); 
  }
}
catch( err )
{
  console.log( JSON.stringify( { Error: `${err.name}: ${err.message}`, Stack: err.stack } ) );
} 
