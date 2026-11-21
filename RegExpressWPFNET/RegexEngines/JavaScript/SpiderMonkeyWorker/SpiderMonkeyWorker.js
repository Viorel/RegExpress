"use strict";

try
{
  let input_text = "";
  for(;;)
  {
    const s = readline();

    if(s === undefined ) {
        break;
    }

    if(s === null ) {
        break;
    }

    const eof_index = s.indexOf("\u001A");

    if(eof_index >= 0)
    {
        s = s.substring(0, eof_index);
    }

    if(input_text != "") input_text += "\r\n";
    input_text += s;

    if(eof_index >= 0) break;
  }

  const input_object = JSON.parse(input_text);

  const pattern = input_object.pattern;
  const text = input_object.text;
  let flags = input_object.flags;
  const func = input_object.func;

  //console.log(``);
  //console.log(`pattern is "${pattern}"`);
  //console.log(`text is "${text}"`);
  //console.log(`flags is "${flags}"`);
  //console.log(`func is "${func}"`);

  if( ! flags.includes("d")) flags += "d";

  if( func === "exec")
  {
    const re = new RegExp( pattern, flags);
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
    const re = new RegExp( pattern, flags);
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
  console.log( JSON.stringify( { Error: `${err.name}: ${err.message}` } ) ); 
} 
