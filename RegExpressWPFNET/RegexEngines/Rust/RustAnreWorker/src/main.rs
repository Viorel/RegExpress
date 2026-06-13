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

    //println!("INPUT: '{}'", input);

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

    let is_anre = options["anre"].as_bool().unwrap_or(false);

    let re;

    if is_anre
    {
        re = regex_anre::Regex::from_anre(pattern);
    }
    else 
    {
        re = regex_anre::Regex::new(pattern);
    }

    if re.is_err()
    {
        let err = re.err().unwrap();

        eprintln!("{}", err);

        return;
    }

    let re = re.unwrap();


    let mut matches = json::JsonValue::new_array();

    for cap in re.captures_iter(text) 
    {

        let mut groups = json::JsonValue::new_array();

        for i in 0..cap.len()
        {
            let g = cap.get( i );
            let group;

            if g.is_some()
            {
                let g = g.unwrap();

                group = json::object!
                {
                    "n" : g.name,
                    "r" : json::array![g.start(), g.end()],
                };
            }
            else
            {
                // (never here?)
                
                group = json::object!
                {
                    "n" : "", //?
                    "r" : json::array![-1, -1],
                };
            }

            groups.push(group).unwrap();
        }

        matches.push(groups).unwrap();

    }

    let output = json::object!
    {
        matches: matches
    };

    let output_json = json::stringify(output);

    println!("{}", output_json);

    return;
}
