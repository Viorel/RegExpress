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

    let structure = input_json["struct"].as_str().unwrap_or("");
    let pattern = input_json["pattern"].as_str().unwrap_or("");
    let text = input_json["text"].as_str().unwrap_or("");
    let options = &input_json["options"];

    let re;

    if structure == "" || structure == "Regex"
    {
        re = fancy_regex::Regex::new(pattern);
    }
    else if structure == "RegexBuilder"
    {
        let mut reb = fancy_regex::RegexBuilder::new(pattern);

        reb.case_insensitive(options["case_insensitive"].as_bool().unwrap_or(false));
        reb.multi_line(options["multi_line"].as_bool().unwrap_or(false));
        reb.ignore_whitespace(options["ignore_whitespace"].as_bool().unwrap_or(false));
        reb.dot_matches_new_line(options["dot_matches_new_line"].as_bool().unwrap_or(false));
        reb.crlf(options["crlf"].as_bool().unwrap_or(false));
        reb.unicode_mode(options["unicode"].as_bool().unwrap_or(false));
        reb.oniguruma_mode(options["oniguruma_mode"].as_bool().unwrap_or(false));
        reb.find_not_empty(options["find_not_empty"].as_bool().unwrap_or(false));
        reb.ignore_numbered_groups_when_named_groups_exist(options["ignore_numbered_groups_when_named_groups_exist"].as_bool().unwrap_or(false));

        let n = options["bl"].as_usize();
        if n.is_some()
        {
            reb.backtrack_limit( n.unwrap());
        } 

        let n = options["dsl"].as_usize();
        if n.is_some()
        {
            reb.delegate_size_limit( n.unwrap());
        } 

        let n = options["ddsl"].as_usize();
        if n.is_some()
        {
            reb.delegate_dfa_size_limit( n.unwrap());
        } 

        re = reb.build();
    }
    else
    {
        eprintln!("Invalid 's': {:?}", structure);

        return;
    }

    if re.is_err()
    {
        let err = re.unwrap_err();

        //eprintln!("Failed to parse the pattern.");
        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();

    let mut names = json::JsonValue::new_array();
    let mut matches = json::JsonValue::new_array();

    for name in re.capture_names()
    {
        names.push(name.unwrap_or("")).unwrap();
    }

    for cap0 in re.captures_iter(text) 
    {
        if cap0.is_err()
        {
            let err = cap0.unwrap_err();
            eprintln!("{}", err);

            return;
        }

        let cap = cap0.unwrap();

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

            groups.push(group).unwrap();
        }

        matches.push(groups).unwrap();
    }

    let output = json::object!
    {
        names: names,
        matches: matches
    };

    let output_json = json::stringify(output);

    println!("{}", output_json);

    return;
}
