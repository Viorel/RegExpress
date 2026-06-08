#![allow(non_snake_case)]

use std::io::Read;

fn main() {

    let mut input = String::new();

    let r = std::io::stdin().read_to_string( & mut input );

    if r.is_err()
    {
        let err = r.unwrap_err();

        eprintln!("Failed to read from 'stdin'");
        eprintln!("{}", err);

        return;
    }

    let input = input.trim();

    let parsed = json::parse(&input);

    if parsed.is_err()
    {
        let err = parsed.unwrap_err();

        eprintln!("Failed to parse input: {}", err);
        eprintln!("Input: '{}'", input);

        return;
    }

    let parsed = parsed.unwrap();

    if ! parsed.is_object()
    {
        eprintln!("Bad json: {}", input);

        return;
    }

    let pattern = parsed["pattern"].as_str().unwrap_or("");
    let text = parsed["text"].as_str().unwrap_or("");
    let options = parsed["options"].as_str().unwrap_or(""); 
    let max_dfa_capacity = parsed["max_dfa_capacity"].as_usize();
    let lookahead_context_max= parsed["lookahead_context_max"].as_u32();

    // println!("Input: '{}'", input);
    // println!(" Pattern: '{}'", pattern);
    // println!("    Text: '{}'", text);
    // println!(" Options: '{}'", options);

    let mut opts = resharp::RegexOptions::default();

    opts.case_insensitive = options.contains(" i ");
    opts.dot_matches_new_line = options.contains(" s ");
    opts.multiline = options.contains(" m ");
    opts.ignore_whitespace = options.contains(" x ");
    opts.hardened = options.contains(" H ");
    opts.unbounded_size = options.contains(" S ");

    if options.contains(" UA ")
    {
        opts.unicode = resharp::UnicodeMode::Ascii;
    }
    else if options.contains(" UF ") {
        opts.unicode = resharp::UnicodeMode::Full;
        
    }
    else if options.contains(" UJ ") {
        opts.unicode = resharp::UnicodeMode::Javascript;
    }

    if max_dfa_capacity.is_some()
    {
        opts.max_dfa_capacity = max_dfa_capacity.unwrap();
    }

    if lookahead_context_max.is_some()
    {
        opts.lookahead_context_max = lookahead_context_max.unwrap();
    }

    let re = resharp::Regex::with_options(pattern, opts);

    if re.is_err()
    {
        let err = re.err().unwrap();

        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();

    let matches = re.find_all(text.as_bytes()).unwrap();

    //println!("Matches: '{:?}'", matches);
    //println!("Matches: '{:#?}'", matches);

    let mut output_matches = json::JsonValue::new_array();

    for m in matches
    {
        let output_match = json::array![ m.start, m.end ];
        
        output_matches.push(output_match).unwrap();
    }

    let output = json::object!
    {
        matches: output_matches
    };


    let output_json = json::stringify(output);

    println!("{}", output_json);

}
