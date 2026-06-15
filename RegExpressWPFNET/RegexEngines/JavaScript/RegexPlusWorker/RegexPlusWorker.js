import * as std from "std";
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

  let adjusted_flags = flags.replace(/[vuxn]/g, ""); // to avoid "Implicit flags v/u/x/n cannot be explicitly added"

  if( ! adjusted_flags.includes("d")) adjusted_flags += "d"; // "d" -- generate indices

  const options =
  {
    flags: adjusted_flags,
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
    let r = [ ]; 
    let m; 
    let l = -2;
    while( (m = re.exec(text)) !== null)
    {
      if( l == re.lastIndex ) break; 
      l = re.lastIndex;
      r.push( { i: m.indices, g: m.indices.groups } );
    } 
    
    console.log( JSON.stringify( { Matches: r } ) );
  }
  else if( func === "matchAll")
  {
    let r = [ ];
    for( const m of text.matchAll( re ) )
    {
      r.push( { i: m.indices, g: m.indices.groups } );
    }

    console.log( JSON.stringify( { Matches: r } ) );
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
