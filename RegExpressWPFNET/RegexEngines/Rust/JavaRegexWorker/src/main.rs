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

        eprintln!("Failed to read from 'stdin'");
        eprintln!("{}", err);

        return;
    }

    let input = input.trim();

//println!("D: Input '{}'", input);

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
    let flags = input_json["flags"].as_str().unwrap_or("");

    let re = java_regex::Regex::with_flags(pattern, flags);

    if re.is_err()
    {
        let err = re.unwrap_err();

        //eprintln!("Failed to parse the pattern.");
        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();
    let mut matches = json::JsonValue::new_array();

    for one_found in re.find_iter(text)
    {
        let mut groups = json::JsonValue::new_array();

        for gp in one_found.group_positions
        {
            match gp
            {
                Some(p) => {groups.push(json::array![p.0, p.1]).unwrap();},
                None => {groups.push(json::array![]).unwrap();}
            }
        }

        // for g in one_match.groups
        // {
        // }

        let mut named_groups = json::JsonValue::new_array();

        for ng in one_found.named_groups
        {
            named_groups.push(json::array![ng.0, ng.1]).unwrap();
        }

        let one_match = json::object!
        {
            m: json::array![one_found.start, one_found.end],
            g: groups,
            ng: named_groups
        };

        matches.push(one_match).unwrap();

    }

    let output = json::object! {matches: matches};

    let output_json = json::stringify(output);

    println!("{}", output_json);

}
