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

    let command = parsed["c"].as_str().unwrap_or("");

    if ! (command == "" || command == "m")
    {
        eprintln!("Bad command: '{}'", command);

        return;
    }

    let structure = parsed["s"].as_str().unwrap_or("");
    let pattern = parsed["p"].as_str().unwrap_or("");
    let text = parsed["t"].as_str().unwrap_or("");
    let options = parsed["o"].as_str().unwrap_or("");

    let re;

    if structure == "" || structure == "Regex"
    {
        re = fancy_regex::Regex::new(pattern);
    }
    else if structure == "RegexBuilder"
    {
        let mut reb = fancy_regex::RegexBuilder::new(pattern);

        reb.case_insensitive(options.find('i').is_some());
        reb.multi_line(options.find('m').is_some());
        reb.ignore_whitespace(options.find('x').is_some());
        reb.dot_matches_new_line(options.find('s').is_some());
        reb.unicode_mode(options.find('u').is_some());
        reb.oniguruma_mode(options.find('O').is_some());

        let s = parsed["bl"].as_str().unwrap_or("");

        if s != ""
        {
            let n = s.parse::<usize>();

            if n.is_err()
            {
                eprintln!("Invalid 'backtrack_limit': '{}'", s);

                return;
            }

            reb.backtrack_limit( n.unwrap());
        }

        let s = parsed["dsl"].as_str().unwrap_or("");

        if s != ""
        {
            let n = s.parse::<usize>();

            if n.is_err()
            {
                eprintln!("Invalid 'delegate_size_limit': '{}'", s);

                return;
            }

            reb.delegate_size_limit( n.unwrap());
        }

        let s = parsed["ddsl"].as_str().unwrap_or("");

        if s != ""
        {
            let n = s.parse::<usize>();

            if n.is_err()
            {
                eprintln!("Invalid 'delegate_dfa_size_limit': '{}'", s);

                return;
            }

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
