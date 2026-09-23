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
    //let options = &input_json["options"];

    // Not documented
    //let reb = derivre::RegexBuilder::new();
    //let reb = reb.ignore_whitespace(true);

    let re = derivre::Regex::new( pattern);

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!( "{}", err);

        return;
    }

    let mut re = re.unwrap();

    let is_match = re.is_match( text);
    let lookahead_len = re.lookahead_len( text);

    let output = json::object!
    {
        is_match: is_match,
        A_len: text.len(),
        B_len: lookahead_len.unwrap_or( 0),
    };

    let output_json = json::stringify( output);

    println!( "{}", output_json);

}
