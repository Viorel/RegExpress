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
    let options = &input_json["options"];

    let mode = match options["mode"].as_str().unwrap_or( "").to_lowercase().as_str()
    {
        "full" => iregexp::MatchMode::Full,
        "search" | "" => iregexp::MatchMode::Search,
        _ => panic!( "Invalid mode")
    };

    let re = iregexp::IRegexp::compile( pattern, mode);

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!( "{}", err);

        return;
    }

    let re = re.unwrap();

    let is_match = re.is_match( text);

    let output = json::object!
    {
        is_match: is_match
    };

    let output_json = json::stringify( output);

    println!( "{}", output_json);

}
