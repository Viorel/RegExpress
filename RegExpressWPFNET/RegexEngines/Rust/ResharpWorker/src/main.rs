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

    // println!("Input: '{}'", input);
    // println!(" Pattern: '{}'", pattern);
    // println!("    Text: '{}'", text);
    // println!(" Options: '{}'", options);

    let mut opts = resharp::RegexOptions::default();

    opts.case_insensitive = options["case_insensitive"].as_bool().unwrap_or(false);
    opts.dot_matches_new_line = options["dot_matches_new_line"].as_bool().unwrap_or(false);
    opts.multiline = options["multi_line"].as_bool().unwrap_or(false);
    opts.ignore_whitespace = options["ignore_whitespace"].as_bool().unwrap_or(false);
    opts.unbounded_size = options["unbounded_size"].as_bool().unwrap_or(false);
    opts.implicit_captures = options["implicit_captures"].as_bool().unwrap_or(false);

    let val = options["unicode_mode"].as_str().unwrap_or("");
    opts.unicode = match val
    {
        "Ascii" => resharp::UnicodeMode::Ascii,
        "Full" => resharp::UnicodeMode::Full,
        "Javascript" => resharp::UnicodeMode::Javascript,
        "" => resharp::UnicodeMode::default(),
        _ => panic!("Invalid unicode_mode: '{}'", val)
    };

    let val = options["hardening"].as_str().unwrap_or("");
    opts.hardening = match val
    {
        "Off" => resharp::HardeningMode::Off,
        "Auto" => resharp::HardeningMode::Auto,
        "Always" => resharp::HardeningMode::Always,
        "" => resharp::HardeningMode::default(),
        _ => panic!("Invalid hardening: '{}'", val)
    };

    let n = options["max_dfa_capacity"].as_usize();
    if n.is_some()
    {
        opts.max_dfa_capacity = n.unwrap();
    }

    let n = options["max_node_capacity"].as_usize();
    if n.is_some()
    {
        opts.max_node_capacity = n.unwrap();
    }

    let n= options["lookahead_context_max"].as_u32();
    if n.is_some()
    {
        opts.lookahead_context_max = n.unwrap();
    }

    let re = resharp::Regex::with_options(pattern, opts);

    if re.is_err()
    {
        let err = re.err().unwrap();
        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();
    let captures = re.captures_all(text.as_bytes());

    if captures.is_err()
    {
        let err = captures.err().unwrap();
        eprintln!("{}", err);

        return;
    }

    let mut output_matches = json::array![];

    for c in captures.unwrap()
    {
        let mut one_match = json::array![];

        for s in c.spans()
        {
            if let Some(t) = *s
            {
                one_match.push(t.0).unwrap();
                one_match.push(t.1).unwrap();
            }
            else
            {
                one_match.push(-1).unwrap();
                one_match.push(-1).unwrap();
            }
        }

        output_matches.push(one_match).unwrap();
    }

    let names = re.capture_names();

    let output = json::object!
    {
        names: names,
        matches: output_matches
    };

    let output_json = json::stringify(output);

    println!("{}", output_json);

}
