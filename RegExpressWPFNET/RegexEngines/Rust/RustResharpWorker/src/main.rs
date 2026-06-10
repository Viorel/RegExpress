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

    let input_json = json::parse(&input);

    if input_json.is_err()
    {
        let err = input_json.unwrap_err();

        eprintln!("Failed to parse input: {}", err);
        eprintln!("Input: '{}'", input);

        return;
    }

    let input_json = input_json.unwrap();

    if ! input_json.is_object()
    {
        eprintln!("Bad json: {}", input);

        return;
    }

    let pattern = input_json["pattern"].as_str().unwrap_or("");
    let text = input_json["text"].as_str().unwrap_or("");
    let options = &input_json["options"];

    let max_dfa_capacity = options["max_dfa_capacity"].as_usize();
    let lookahead_context_max= options["lookahead_context_max"].as_u32();

    // println!("Input: '{}'", input);
    // println!(" Pattern: '{}'", pattern);
    // println!("    Text: '{}'", text);
    // println!(" Options: '{}'", options);

    let mut opts = resharp::RegexOptions::default();

    opts.case_insensitive = options["case_insensitive"].as_bool().unwrap_or(false);
    opts.dot_matches_new_line = options["dot_matches_new_line"].as_bool().unwrap_or(false);
    opts.multiline = options["multi_line"].as_bool().unwrap_or(false);
    opts.ignore_whitespace = options["ignore_whitespace"].as_bool().unwrap_or(false);
    opts.hardened = options["hardened"].as_bool().unwrap_or(false);
    opts.unbounded_size = options["unbounded_size"].as_bool().unwrap_or(false);

    let unicode_mode = options["unicode_mode"].as_str().unwrap_or("");

    if unicode_mode == "Ascii"
    {
        opts.unicode = resharp::UnicodeMode::Ascii;
    }
    else if unicode_mode == "Full"
    {
        opts.unicode = resharp::UnicodeMode::Full;
        
    }
    else if unicode_mode == "Javascript"
    {
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

    let matches = re.find_all(text.as_bytes());

    if matches.is_err()
    {
        let err = matches.err().unwrap();

        eprintln!("{}", err);

        return;
    }

    let matches = matches.unwrap();

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
