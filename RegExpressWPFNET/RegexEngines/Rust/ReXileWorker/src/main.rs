#![allow(non_snake_case)]
//#![allow(unused_imports)]
//#![allow(unused_variables)]
//#![allow(unreachable_code)]

use std::io::Read;

fn main() 
{
    let mut input = String::new();

    let r = std::io::stdin().read_to_string( & mut input );

    if r.is_err()
    {
        let err = r.unwrap_err();

        eprintln!( "Failed to read from 'stdin'");
        eprintln!( "{}", err);

        return;
    }

    let input = input.trim();

//println!("D: Input '{}'", input);

    let input_json = json::parse( &input);

    if input_json.is_err()
    {
        let err = input_json.unwrap_err();

        eprintln!( "Failed to parse input: {}", err);
        eprintln!( "Input: '{}'", input);

        return;
    }

    let input_json = input_json.unwrap();

    if ! input_json.is_object()
    {
        eprintln!( "Bad json: {}", input);

        return;
    }

    let pattern = input_json["pattern"].as_str().unwrap_or( "");
    let text = input_json["text"].as_str().unwrap_or( "");
    //let options = &input_json["options"];

    let re = rexile::Pattern::new( pattern);

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();

    let mut matches = json::array![];

    for cap in re.captures_iter(text)
    {
        let mut groups = json::array![];

        for i in 0..cap.len()
        {

            match cap.pos(i)
            {
                None => groups.push(json::array![-1, -1]).unwrap(),
                Some(pos) => groups.push(json::array![pos.0, pos.1]).unwrap()
            }
        }


        // for g in cap.iter() // strings
        // {
        // }

        matches.push(groups).unwrap();
    }

    let output = json::object!
    {
        //names: names,
        matches: matches
    };

    let output_json = json::stringify( output);

    println!( "{}", output_json);

}
