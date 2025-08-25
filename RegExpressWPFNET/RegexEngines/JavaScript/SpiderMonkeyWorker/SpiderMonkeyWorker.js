try
{
  let input_text = "";
  for(;;)
  {
    let s = readline();

    if(s === undefined ) {
        break;
    }

    if(s === null ) {
        break;
    }

    let eof_index = s.indexOf("\u001A");

    if(eof_index >= 0)
    {
        s = s.substring(0, eof_index);
    }

    if(input_text != "") input_text += "\r\n";
    input_text += s;

    if(eof_index >= 0) break;
  }

  let input_object = JSON.parse(input_text);

  let cmd = input_object.cmd;

  if( cmd === "version")
  {
    console.log( JSON.stringify( { version: "C143.0" } ) ); // TODO: get programmatically
  }
  else
  {
    let pattern = input_object.pattern;
    let text = input_object.text;
    let flags = input_object.flags;
    let func = input_object.func;

    //console.log(``);
    //console.log(`pattern is "${pattern}"`);
    //console.log(`text is "${text}"`);
    //console.log(`flags is "${flags}"`);
    //console.log(`func is "${func}"`);

    if( ! flags.includes("d")) flags += "d";

    if( func === "exec")
    {
      let re = new RegExp(pattern, flags);
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
      let re = new RegExp(pattern, flags);
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
}
catch( err )
{
  console.log( JSON.stringify( { Error: err.message } ) ); 
} 
