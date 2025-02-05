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

    if command == "v"
    {
        let v = rustc_version_runtime::version();
        let output = json::object!
        {
            version: std::format!("{}.{}.{}", v.major, v.minor, v.patch)
        };
        
        let output_json = json::stringify(output);
        
        println!("{}", output_json);
        
        return;
    }

    if ! (command == "" || command == "m")
    {
        eprintln!("Bad command: '{}'", command);

        return;
    }

    let pattern = parsed["p"].as_str().unwrap_or("");
    let text = parsed["t"].as_str().unwrap_or("");
    let options = parsed["o"].as_str().unwrap_or("");

    let mut flags = regress::Flags::new(std::iter::empty::<u32>());
    flags.icase = options.find('i').is_some();
    flags.multiline = options.find('m').is_some();
    flags.dot_all = options.find('s').is_some();
    flags.no_opt = options.find('N').is_some();
    flags.unicode = options.find('u').is_some();
    flags.unicode_sets = options.find('v').is_some();

    match regress::Regex::with_flags(pattern, flags)
    {
        Ok(re) =>
        {
            let mut matches = json::JsonValue::new_array();

            for m in re.find_iter(text) 
            {
                //println!("{}", &text[m.range()]);
                //println!(" range: {}..{}", m.range.start, m.range.end );
                //println!(" groups cnt: {}", m.groups().count());

                let mut groups = json::JsonValue::new_array();

                for g in m.groups()
                {
                    //println!("    succ: {}", g.is_some());
                    //println!("    start: {}", if g.is_some() {g.unwrap().start as i32} else {-1});

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

                //println!(" named_groups: {}", m.named_groups().count());

                let mut named_groups = json::JsonValue::new_array();

                for (n, v) in m.named_groups()
                {
                    //println!("    n: {}", n);
                    //println!("    succ: {}", v.is_some());
                    //println!("    start: {}", if v.is_some() {v.unwrap().start as i32} else {-1});

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
