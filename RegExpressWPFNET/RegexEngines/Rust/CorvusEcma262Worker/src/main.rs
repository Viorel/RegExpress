#![allow(non_snake_case)]

use std::{io::Read, time::Duration};

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
    let options = &input_json["options"];

    let flags = corvus_regex_ecma262::Flags
    { 
        has_indices: options["has_indices"].as_bool().unwrap_or( false),
        global: options["global"].as_bool().unwrap_or( false),
        ignore_case: options["ignore_case"].as_bool().unwrap_or( false),
        multiline: options["multiline"].as_bool().unwrap_or( false),
        dot_all: options["dot_all"].as_bool().unwrap_or( false),
        unicode: options["unicode"].as_bool().unwrap_or( false),
        unicode_sets: options["unicode_sets"].as_bool().unwrap_or( false),
        sticky: options["sticky"].as_bool().unwrap_or( false),
        //..Default::default() 
    };

    let unicode_version_s = options["unicode_version"].as_str().unwrap_or("");
    let unicode_version: corvus_regex_ecma262::UnicodeVersion =
        match unicode_version_s
        {
            "Default" | "" => corvus_regex_ecma262::UnicodeVersion::Default,
            "V13" => corvus_regex_ecma262::UnicodeVersion::V13,
            "V17" => corvus_regex_ecma262::UnicodeVersion::V17,
            "V18" => corvus_regex_ecma262::UnicodeVersion::V18,
            "Latest" => corvus_regex_ecma262::UnicodeVersion::Latest,
            _ => panic!("Unsupported Unicode Version: '{}'", unicode_version_s)
        };

    let mut timeout: Option<Duration> = None;
    let n = options["timeout"].as_u64();
    if let Some(n) = n
    {
        timeout = Some( std::time::Duration::from_millis( n));
    } 

    let re = corvus_regex_ecma262::RegexBuilder::new( pattern)
        .with_flags(flags)
        .unicode_version(unicode_version)
        .match_timeout(timeout)
        .build();

    if re.is_err()
    {
        let err = re.unwrap_err();
        eprintln!( "{}", err);

        return;
    }

    let re = re.unwrap();

    let mut names = json::array![];
    for name in re.group_names()
    {
        names.push( name).unwrap();
    }

    let mut matches = json::array![];

    for cap in re.captures_iter(text)
    {
        if cap.is_err()
        {
            let err = cap.unwrap_err();
            eprintln!( "{}", err);

            return;
        }

        let cap = cap.unwrap();

        let mut groups = json::array![];

        for g in cap.iter()
        {
            if g.is_none()
            {
                groups.push( -1).unwrap();
                groups.push( -1).unwrap();
            }
            else 
            {
                let g = g.unwrap();

                groups.push( g.start()).unwrap();
                groups.push( g.end()).unwrap();
            }
        }

        let mut named_groups = json::array![];

        for name in re.group_names()
        {
            let m = cap.name(name);

            if m.is_none()
            {
                named_groups.push( -1).unwrap();
                named_groups.push( -1).unwrap();
            }
            else 
            {
                let m = m.unwrap();
                named_groups.push( m.start()).unwrap();
                named_groups.push( m.end()).unwrap();
            }
        }

        matches.push( json::object! { g: groups, n: named_groups }).unwrap();

    }

    let output = json::object!
    {
        Names: names,
        Matches: matches
    };

    let output_json = json::stringify( output);

    println!( "{}", output_json);

}
