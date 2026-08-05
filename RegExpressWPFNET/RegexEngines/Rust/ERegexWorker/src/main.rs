use std::{io::Read};

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

    let mut flags = eregex::Flags::NONE;

    if options["IGNORECASE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::IGNORECASE;
    }
    if options["MULTILINE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::MULTILINE;
    }
    if options["DOTALL"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::DOTALL;
    }
    if options["UNICODE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::UNICODE;
    }
    if options["ASCII"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::ASCII;
    }
    if options["VERBOSE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::VERBOSE;
    }
    if options["FULLCASE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::FULLCASE;
    }
    if options["WORD"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::WORD;
    }
    if options["LOCALE"].as_bool().unwrap_or(false)
    {
        flags |= eregex::Flags::LOCALE;
    }

    let re = eregex::Regex::new_with_flags(pattern, flags);

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();

    let mut group_names = json::array![];

    for (n, i) in re.group_names()
    {
        group_names.push( json::object! { n : n.clone(), i : *i }).unwrap();
    }

    let mut matches = json::JsonValue::new_array();

    for m in re.captures_iter(text)
    {
        let mut one_match = json::array![];

        for (group_index, group_value) in m.all_groups().iter().enumerate()
        {
            let one_group;

            if ! group_value.is_some()
            {
                one_group = json::object! 
                {
                    g : json::array![ ],
                    c : json::Null,
                };
            }
            else 
            {
                let mut captures = json::array![];

                if group_index > 0
                {
                    for c in m.spans(group_index)
                    {
                        captures.push(json::array![c.0, c.1]).unwrap();
                    }
                }

                one_group = json::object! 
                {
                    g : json::array![m.start_of( group_index), m.end_of( group_index)],
                    c : captures,
                };
            }

            one_match.push( one_group).unwrap();

        }

        matches.push( one_match).unwrap();

    }

    let output = json::object!
    {
        matches: matches,
        names: group_names,
    };

    let output_json = json::stringify(output);

    println!("{}", output_json);
}
