#![allow(non_snake_case)]
//#![allow(unused_imports)]
//#![allow(unused_variables)]
//#![allow(unreachable_code)]

use std::io::Read;

fn main() 
{
    let mut input = String::new();

    let r = std::io::stdin().read_to_string( & mut input );

    if let Err(err) = r
    {
        eprintln!( "Failed to read from 'stdin'");
        eprintln!( "{}", err);

        return;
    }

    let input = input.trim();

    let input_json = json::parse( &input);

    if let Err(err) = input_json
    {
        eprintln!( "Failed to parse input: {}", err);
        eprintln!( "Input: '{}'", input);

        return;
    }

    let input_json = input_json.unwrap();

    if ! input_json.is_object()
    {
        eprintln!( "Bad json: {}", input);

        return;
    }

    let pattern = input_json["pattern"].as_str().unwrap_or( "");
    let text = input_json["text"].as_str().unwrap_or( "");
    let input_options = &input_json["options"];

    let mut options = rusty_expressions::Options::NONE;

    if input_options["IGNORECASE"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::IGNORECASE; };
    if input_options["EXTEND"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::EXTEND; };
    if input_options["MULTILINE"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::MULTILINE; };
    if input_options["SINGLELINE"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::SINGLELINE; };
    if input_options["FIND_LONGEST"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::FIND_LONGEST; };
    if input_options["FIND_NOT_EMPTY"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::FIND_NOT_EMPTY; };
    if input_options["NEGATE_SINGLELINE"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NEGATE_SINGLELINE; };
    if input_options["DONT_CAPTURE_GROUP"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::DONT_CAPTURE_GROUP; };
    if input_options["CAPTURE_GROUP"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::CAPTURE_GROUP; };
    if input_options["NOTBOL"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NOTBOL; };
    if input_options["NOTEOL"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NOTEOL; };
    if input_options["IGNORECASE_IS_ASCII"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::IGNORECASE_IS_ASCII; };
    if input_options["WORD_IS_ASCII"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::WORD_IS_ASCII; };
    if input_options["DIGIT_IS_ASCII"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::DIGIT_IS_ASCII; };
    if input_options["SPACE_IS_ASCII"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::SPACE_IS_ASCII; };
    if input_options["POSIX_IS_ASCII"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::POSIX_IS_ASCII; };
    if input_options["TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER; };
    if input_options["TEXT_SEGMENT_WORD"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::TEXT_SEGMENT_WORD; };
    if input_options["NOT_BEGIN_STRING"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NOT_BEGIN_STRING; };
    if input_options["NOT_END_STRING"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NOT_END_STRING; };
    if input_options["NOT_BEGIN_POSITION"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::NOT_BEGIN_POSITION; };
    if input_options["CALLBACK_EACH_MATCH"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::CALLBACK_EACH_MATCH; };
    if input_options["MATCH_WHOLE_STRING"].as_bool().unwrap_or( false) { options = options | rusty_expressions::Options::MATCH_WHOLE_STRING; };

    let syntax = match input_options["syntax"].as_str().unwrap_or( "")
        {
            "OnigSyntaxASIS" => rusty_expressions::Syntax::ASIS,
            "OnigSyntaxEmacs" => rusty_expressions::Syntax::emacs(),
            "OnigSyntaxGnuRegex" => rusty_expressions::Syntax::gnu_regex(),
            "OnigSyntaxGrep" => rusty_expressions::Syntax::grep(),
            "OnigSyntaxJava" => rusty_expressions::Syntax::java(),
            "OnigSyntaxOniguruma" | "" => rusty_expressions::Syntax::ONIGURUMA,
            "OnigSyntaxPerl" => rusty_expressions::Syntax::perl(),
            "OnigSyntaxPerl_NG" => rusty_expressions::Syntax::perl_ng(),
            "OnigSyntaxPosixBasic" => rusty_expressions::Syntax::posix_basic(),
            "OnigSyntaxPosixExtended" => rusty_expressions::Syntax::posix_extended(),
            "OnigSyntaxPython" => rusty_expressions::Syntax::python(),
            //"OnigSyntaxRuby" => rusty_expressions::Syntax::ruby(), // (missing)
            _ => panic!("Invalid syntax: '{}'", input_options["syntax"].as_str().unwrap_or( ""))
        };

    let mut mp = rusty_expressions::MatchParam::default();

    let n = input_options["stack_limit"].as_u32();
    if let Some(n) = n
    {
        mp.stack_limit = n;
    } 

    let n = input_options["retry_limit_in_match"].as_u64();
    if let Some(n) = n
    {
        mp.retry_limit_in_match = n;
    } 

    let n = input_options["retry_limit_in_search"].as_u64();
    if let Some(n) = n
    {
        mp.retry_limit_in_search = n;
    } 

    let n = input_options["subexp_call_limit"].as_u32();
    if let Some(n) = n
    {
        mp.subexp_call_limit = n;
    } 

    let re = rusty_expressions::Regex::new( 
            pattern, 
            options,
            rusty_expressions::Encoding::UTF8,
            syntax
            );

    if let Err(err) = re
    {
        eprintln!( "{}", err);

        return;
    }

    let re = re.unwrap();

    let text_bytes = text.as_bytes();
    let mut start = 0;

    let mut all_matches = json::array![];
    let mut all_names: Option<Vec<Option<String>>> = None;

    while start <= text_bytes.len()
    {
        let native_match = re.search_range_param( text_bytes, start, text_bytes.len(), &mp);

        if let Err( err) = native_match
        {
            eprintln!( "{}", err);

            return;
        }

        let native_match = native_match.unwrap();

        if native_match.is_none()
        {
            break;
        }

        let native_match = native_match.unwrap();

        if all_names.is_none()
        {
            all_names = Some( native_match.names.clone());
        }

        let mut one_match = json::array![];
        let mut group_index = 0;
    
        for native_group in &native_match.captures
        {
            let mut main_group = json::array![];
            let mut captures = json::array![];

            match native_group
            {
                Some( native_group) => 
                {
                    main_group.push( native_group.start).unwrap();
                    main_group.push( native_group.end).unwrap();

                    native_match.traverse_history(|n, _|
                    {
                        if n.group == 0 || n.group != group_index
                        {
                            return;
                        }
                        
                        //println!( "CAP {} {} {} {}", n.group, n.range.start, n.range.end, d);

                        captures.push( n.range.start).unwrap();
                        captures.push( n.range.end).unwrap();
                    });

                },
                None => 
                {
                    main_group.push( -1).unwrap();
                    main_group.push( -1).unwrap();
                }
            }

            let one_group = json::object! 
            {
                g: main_group,
                c: captures, //( !captures.is_empty()).then_some( captures) // non-"idiotomatic": if captures.len() == 0 { None } else { Some(captures) }
            };

            one_match.push( one_group).unwrap();

            group_index += 1;
        }

        all_matches.push( one_match).unwrap();

        // next start

        if native_match.range().end <= start // '==start' for empty match
        {
            let mut new_start = start;
            let mut char_indices = text.char_indices();

            loop
            {
                let a = char_indices.next();
                if a.is_none()
                {
                    break;
                }

                let a = a.unwrap();
                if a.0 == start
                {
                    let b = char_indices.next();

                    if let Some( b) = b
                    {
                        new_start = b.0;
                    }

                    break;
                }
            }

            if start >= new_start
            {
                break;
            }

            start = new_start;
        }
        else 
        {
            start = native_match.range().end;   
        }

    }

    let output = json::object! 
        {
            matches: all_matches,
            names: all_names,
        };

    let output_json = json::stringify(output);

    println!( "{}", output_json);

}
