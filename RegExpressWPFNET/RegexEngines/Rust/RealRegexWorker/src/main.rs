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


    if ! input_json["use_builder"].is_boolean()
    {
        eprintln!("\"use_builder\" not specified");

        return;
    }

    let use_builder = input_json["use_builder"].as_bool().unwrap_or(false);
    let pattern = input_json["pattern"].as_str().unwrap_or( "");
    let text = input_json["text"].as_str().unwrap_or( "");
    let options = &input_json["options"];

    let re;

    if ! use_builder
    {
        re = real_regex::Regex::new( pattern);
    }
    else
    {
        let mut reb = real_regex::RegexBuilder::new(pattern);

        reb.case_insensitive( options["case_insensitive"].as_bool().unwrap_or( false));
        reb.multi_line( options["multi_line"].as_bool().unwrap_or( false));
        reb.dot_matches_new_line( options["dot_matches_new_line"].as_bool().unwrap_or( false));
        //? reb.swap_greed(options["swap_greed"].as_bool().unwrap_or( false));
        reb.ignore_whitespace( options["ignore_whitespace"].as_bool().unwrap_or( false));
        reb.unicode( options["unicode"].as_bool().unwrap_or( false));
        //? reb.octal(options["octal"].as_bool().unwrap_or( false));

        reb.fallback( options["fallback"].as_bool().unwrap_or( false));

        // (no-op)
        // let n = options["sl"].as_usize();
        // if n.is_some()
        // {
        //     reb.size_limit( n.unwrap());
        // } 

        //?
        // let n = options["dsl"].as_usize();
        // if n.is_some()
        // {
        //     reb.dfa_size_limit( n.unwrap());
        // } 

        // let n = options["nl"].as_u32();
        // if n.is_some()
        // {
        //     reb.nest_limit( n.unwrap());
        // } 

        re = reb.build();
    }

    if re.is_err()
    {
        let err = re.unwrap_err();

        //eprintln!("Failed to parse the pattern.");
        eprintln!( "{}", err);

        return;
    }

    let re = re.unwrap();

    let mut names = json::JsonValue::new_array();
    let mut matches = json::JsonValue::new_array();

    for name in re.capture_names()
    {
        names.push( name.unwrap_or( "")).unwrap();
    }

    for cap in re.captures_iter( text) 
    {
        let mut groups = json::JsonValue::new_array();

        for g in cap.iter()
        {
            let group;
            if g.is_some()
            {
                let g = g.unwrap();
                group = json::array![ g.start(), g.end() ];
            }
            else
            {
                group = json::array![ ];
            }

            groups.push( group).unwrap();
        }

        matches.push( groups).unwrap();
    }

    let output = json::object!
    {
        names: names,
        matches: matches
    };

    let output_json = json::stringify( output);

    println!( "{}", output_json);

    return;
}
