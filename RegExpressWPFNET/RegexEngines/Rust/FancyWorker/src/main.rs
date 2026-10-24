#![allow(non_snake_case)]
//#![allow(unused_imports)]
//#![allow(unused_variables)]
//#![allow(unreachable_code)]

use std::io::Read;


fn main()
{

    let mut input_string = String::new();

    let r = std::io::stdin().read_to_string( & mut input_string );

    if r.is_err()
    {
        let err = r.unwrap_err();

        eprintln!( "Failed to read from 'stdin'");
        eprintln!( "{}", err);

        return;
    }

    let input_string = input_string.trim();

//println!("D: Input '{}'", input);

    let input = json::parse( &input_string);

    if input.is_err()
    {
        let err = input.unwrap_err();

        eprintln!( "Failed to parse input: {}", err);
        eprintln!( "Input: '{}'", input_string);

        return;
    }

    let input = input.unwrap();

    if ! input.is_object()
    {
        eprintln!( "Bad json: {}", input_string);

        return;
    }

    if ! input["use_builder"].is_boolean()
    {
        eprintln!( "\"use_builder\" not specified");

        return;
    }

    let use_builder = input["use_builder"].as_bool().unwrap_or( false);
    let pattern = input["pattern"].as_str().unwrap_or( "");
    let text = input["text"].as_str().unwrap_or( "");
    let options = &input["options"];

    let re;

    if ! use_builder
    {
        re = fancy_regex::Regex::new( pattern);
    }
    else 
    {
        let mut reb = fancy_regex::RegexBuilder::new( pattern);

        reb.case_insensitive( options["case_insensitive"].as_bool().unwrap_or( false));
        reb.multi_line( options["multi_line"].as_bool().unwrap_or( false));
        reb.ignore_whitespace( options["ignore_whitespace"].as_bool().unwrap_or( false));
        reb.dot_matches_new_line( options["dot_matches_new_line"].as_bool().unwrap_or( false));
        //reb.verbose_mode -- The same as 'ignore_whitespace'
        reb.unicode_mode( options["unicode"].as_bool().unwrap_or(  false));
        reb.oniguruma_mode( options["oniguruma_mode"].as_bool().unwrap_or( false));
        reb.crlf( options["crlf"].as_bool().unwrap_or( false));
        reb.find_not_empty( options["find_not_empty"].as_bool().unwrap_or( false));
        reb.leftmost_longest( options["leftmost_longest"].as_bool().unwrap_or( false));
        reb.ignore_numbered_groups_when_named_groups_exist( options["ignore_numbered_groups_when_named_groups_exist"].as_bool().unwrap_or( false));
        reb.seek( options["seek"].as_bool().unwrap_or( false));
        reb.disallow_empty_match_at_eof_after_newline( options["disallow_empty_match_at_eof_after_newline"].as_bool().unwrap_or( false));
        reb.allow_input_assertion_overrides( options["allow_input_assertion_overrides"].as_bool().unwrap_or( false));
        reb.delegate_dfa_skip_cache_capacity_check( options["delegate_dfa_skip_cache_capacity_check"].as_bool().unwrap_or( false));

        let n = options["backtrack_limit"].as_usize();
        if n.is_some()
        {
            reb.backtrack_limit( n.unwrap());
        } 

        let n = options["delegate_size_limit"].as_usize();
        if n.is_some()
        {
            reb.delegate_size_limit( n.unwrap());
        } 

        let n = options["delegate_dfa_size_limit"].as_usize();
        if n.is_some()
        {
            reb.delegate_dfa_size_limit( n.unwrap());
        } 

        let n = options["max_prog_size"].as_usize();
        if n.is_some()
        {
            reb.max_prog_size( n.unwrap());
        } 

        let bytes_modes = options["bytes_mode"].as_str().unwrap_or( "Default");
        if bytes_modes != "Default"
        {
            reb.bytes_mode( match bytes_modes
            {
                "Unicode" => fancy_regex::BytesMode::Unicode,
                "Ascii" => fancy_regex::BytesMode::Ascii,
                "UnicodeBytes" => fancy_regex::BytesMode::UnicodeBytes,
                _ => panic!( "Unsupported 'bytes_mode': {}", bytes_modes)
            });
        }

        re = reb.build();
    }

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!( "{}", err);

        return;
    }

    let start_text = options["start_text"].as_bool().unwrap_or( true);
    let end_text = options["end_text"].as_bool().unwrap_or( true);

    let re = re.unwrap();

    let mut names = json::array![];
    let mut matches = json::array![];

    for name in re.capture_names()
    {
        names.push( name.unwrap_or( "")).unwrap();
    }

    for cap0 in re.captures_iter_input( fancy_regex::RegexInput::new(text).start_text( start_text).end_text( end_text)) 
    {
        if cap0.is_err()
        {
            let err = cap0.unwrap_err();
            eprintln!( "{}", err);

            return;
        }

        let cap = cap0.unwrap();

        let mut groups = json::array![];

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
                group = json::array![];
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
