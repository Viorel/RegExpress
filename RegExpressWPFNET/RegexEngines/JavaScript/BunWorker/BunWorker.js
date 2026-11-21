"use strict";

const readline = require("readline");

const rl = readline.createInterface(
  {
    input: process.stdin,
    output: process.stdout,
    terminal: false
  });

let input_text = "";

rl.on('line', (line) => 
{
    input_text += line;
});

rl.once('close', () => 
{
  try
  {
    const input_object = JSON.parse(input_text);

    const cmd = input_object.cmd;

    if( cmd === "version")
    {
      console.log( JSON.stringify( { bun: process.versions.bun } ) );
    }
    else
    {
      const pattern = input_object.pattern;
      const text = input_object.text;
      let flags = input_object.flags;
      const func = input_object.func;

      //console.log(``);
      //console.log(`pattern is "${pattern}"`);
      //console.log(`text is "${text}"`);
      //console.log(`flags is "${flags}"`);
      //console.log(`func is "${func}"`);

      if( ! flags.includes("d")) flags += "d"; // "d" -- generate indices

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
            if( text.codePointAt( match.index) > 0xFFFF)
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
  }
  catch( err )
  {
    console.log( JSON.stringify( { Error: `${err.name}: ${err.message}` } ) ); 
  } 
});
