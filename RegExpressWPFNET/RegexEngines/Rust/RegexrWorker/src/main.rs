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

    if ! input_json["use_builder"].is_boolean()
    {
        eprintln!("\"use_builder\" not specified");

        return;
    }

    let use_builder = input_json["use_builder"].as_bool().unwrap_or(false);
    let pattern = input_json["pattern"].as_str().unwrap_or("");
    let text = input_json["text"].as_str().unwrap_or("");
    let options = &input_json["options"];

    let re;

    if ! use_builder
    {
        re = regexr::Regex::new(pattern);
    }
    else
    {
        let jit = options["jit"].as_bool().unwrap_or(false);
        let optimize_prefixes= options["optimize_prefixes"].as_bool().unwrap_or(false);

        let mut reb = regexr::RegexBuilder::new(pattern)
            .jit(jit)
            .optimize_prefixes(optimize_prefixes)
            ;

        let n = options["backtrack_limit"].as_u64();
        if n.is_some()
        {
            reb = reb.backtrack_limit( n.unwrap());
        } 

        let n = options["size_limit"].as_u32();
        if n.is_some()
        {
            reb = reb.size_limit( n.unwrap());
        } 

        re = reb.build();
    }

    if re.is_err()
    {
        let err = re.unwrap_err();

        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();

    let mut names = json::array![];
    let mut matches = json::array![];

    for name in re.capture_names()
    {
        names.push(name).unwrap();
    }

    // detect "match limit exceeded"; ('captures_iter' does not seem to detect it)

    let tried_captures = re.try_find(text);
    if tried_captures.is_err()
    {
        let err = tried_captures.unwrap_err();

        eprintln!("{}", err);

        return;
    }

    for cap in re.captures_iter(text)
    {
        let mut groups = json::array![];

        for i in 0..cap.len()
        {
            let g = cap.get( i );
            let group;

            if ! g.is_some()
            {
                group = json::array![ ];
            }
            else
            {
                let g = g.unwrap();
                group = json::array![ g.start(), g.end() ];
            }

            groups.push(group).unwrap();
        }

        let mut named_groups = json::array![];

        for name in re.capture_names()
        {
            let g = cap.name( name );

            if ! g.is_some()
            {
                named_groups.push( json::object! 
                    {
                        n: name,
                        g: json::array![ ]
                    }).unwrap();
            }
            else
            {
                let g = g.unwrap();

                named_groups.push( json::object! 
                    {
                        n: name,
                        g: json::array![ g.start(), g.end() ]
                    }).unwrap();
            }
        }

        matches.push(json::object! 
            {
                g: groups,
                ng: named_groups,
            }).unwrap();

    }

    let output = json::object!
    {
        names: names,
        matches: matches
    };

    let output_json = json::stringify(output);

    println!("{}", output_json);

}
