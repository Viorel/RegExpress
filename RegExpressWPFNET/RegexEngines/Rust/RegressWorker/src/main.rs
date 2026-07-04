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
    let options = &input_json["options"];

    let mut flags = regress::Flags::new(std::iter::empty::<u32>());
    flags.icase = options["case_insensitive"].as_bool().unwrap_or(false);
    flags.multiline = options["multi_line"].as_bool().unwrap_or(false);
    flags.dot_all = options["dot_matches_new_line"].as_bool().unwrap_or(false);
    flags.no_opt = options["no_opt"].as_bool().unwrap_or(false);
    flags.unicode = options["unicode"].as_bool().unwrap_or(false);
    flags.unicode_sets = options["unicode_sets"].as_bool().unwrap_or(false);

    match regress::Regex::with_flags(pattern, flags)
    {
        Ok(re) =>
        {
            let mut matches = json::JsonValue::new_array();

            for m in re.find_iter(text) 
            {
                let mut groups = json::JsonValue::new_array();

                for g in m.groups()
                {
                    let group;

                    if g.is_some() 
                    {
                        let gu = g.unwrap();
                        group = json::array![ gu.start, gu.end ];
                    }
                    else
                    {
                        group = json::array![ ];
                    }

                    groups.push(group).unwrap();
                }

                let mut named_groups = json::JsonValue::new_array();

                for (n, v) in m.named_groups()
                {
                    let named_group;

                    if v.is_some()
                    {
                        let vu = v.unwrap();

                        named_group = json::object!
                        {
                            n: n,
                            r: json::array![ vu.start, vu.end ]
                        };
                    }
                    else
                    {
                        named_group = json::object!
                        {
                            n: n,
                            r: json::array![ ]
                        };
                    }

                    named_groups.push(named_group).unwrap();
                }

                let one_match = json::object!
                {
                    g: groups,
                    ng: named_groups
                };

                matches.push(one_match).unwrap();
            }

            let output_json = json::stringify(matches);

            println!("{}", output_json);
        },

        Err(err) =>
        {
            eprintln!("{err}");
        }
    }

    return;
}
