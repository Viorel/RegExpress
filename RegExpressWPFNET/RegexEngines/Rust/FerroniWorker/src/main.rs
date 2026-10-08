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

    let use_builder = input_json["use_builder"].as_bool().unwrap_or( false);
    let pattern = input_json["pattern"].as_str().unwrap_or( "");
    let text = input_json["text"].as_str().unwrap_or( "");
    let options = &input_json["options"];

    let re;
    let mut se = ferroni::api::SearchOptions::default();

    if ! use_builder
    {
        //let re = ferroni::prelude::Regex::new( pattern);
        re = ferroni::api::Regex::new( pattern);
    }
    else
    {
        let mut reb = ferroni::api::RegexBuilder::new( pattern);

        reb = reb
            .case_insensitive( options["case_insensitive"].as_bool().unwrap_or( false))
            .dot_matches_newline( options["dot_matches_newline"].as_bool().unwrap_or( false))
            .multi_line_anchors( options["multi_line_anchors"].as_bool().unwrap_or( false))
            .extended( options["extended"].as_bool().unwrap_or( false))
            .reject_backtracking_risks(options["reject_backtracking_risks"].as_bool().unwrap_or( false))
            .optimize_backtracking(options["optimize_backtracking"].as_bool().unwrap_or( false))
            .syntax( match options["syntax"].as_str().unwrap_or( "")
            {
                "OnigSyntaxASIS" => &ferroni::regsyntax::OnigSyntaxASIS,
                "OnigSyntaxEmacs" => &ferroni::regsyntax::OnigSyntaxEmacs,
                "OnigSyntaxGnuRegex" => &ferroni::regsyntax::OnigSyntaxGnuRegex,
                "OnigSyntaxGrep" => &ferroni::regsyntax::OnigSyntaxGrep,
                "OnigSyntaxJava" => &ferroni::regsyntax::OnigSyntaxJava,
                "OnigSyntaxOniguruma" | "" => &ferroni::regsyntax::OnigSyntaxOniguruma,
                "OnigSyntaxPerl" => &ferroni::regsyntax::OnigSyntaxPerl,
                "OnigSyntaxPerl_NG" => &ferroni::regsyntax::OnigSyntaxPerl_NG,
                "OnigSyntaxPosixBasic" => &ferroni::regsyntax::OnigSyntaxPosixBasic,
                "OnigSyntaxPosixExtended" => &ferroni::regsyntax::OnigSyntaxPosixExtended,
                "OnigSyntaxPython" => &ferroni::regsyntax::OnigSyntaxPython,
                "OnigSyntaxRuby" => &ferroni::regsyntax::OnigSyntaxRuby,
                _ => panic!("Invalid syntax: '{}'", options["syntax"].as_str().unwrap_or( ""))
            });

        let n = options["timeout"].as_u64();
        if let Some(n) = n
        {
            se = se.timeout( std::time::Duration::from_millis(n));
        } 

        let n = options["retry_limit_in_match"].as_u64();
        if let Some(n) = n
        {
            se = se.retry_limit_in_match( n);
        } 

        let n = options["retry_limit_in_search"].as_u64();
        if let Some(n) = n
        {
            se = se.retry_limit_in_search( n);
        } 

        let n = options["match_stack_limit"].as_u32();
        if let Some(n) = n
        {
            se = se.match_stack_limit( n);
        } 

        re = reb.build();
    }

    if let Err(err) = re
    {
        eprintln!( "{}", err);

        return;
    }

    let re = re.unwrap();

    let mut all_matches = json::array![];


    for cap in re.captures_iter_with( &text, se)
    {
        if let Err(err) = cap
        {
            eprintln!( "{}", err);

            return;
        }

        let captures = cap.unwrap();

        let mut one_match = json::array![];

        for c in captures.iter()
        {
            match c
            {
                Some(m) =>
                {
                    one_match.push( m.start()).unwrap();
                    one_match.push( m.end()).unwrap();
                },
                None =>
                {
                    one_match.push( -1).unwrap();
                    one_match.push( -1).unwrap();
                }
            }
        }

        let o = json::object! 
        {
            g : one_match,
            // TODO: names
        };
        all_matches.push( o).unwrap();

    }

    let mut names = json::array![];

    ferroni::regexec::onig_foreach_name( re.as_raw(), |n, i| 
        {
            names.push(
                json::object! 
                {
                    n : str::from_utf8( n).unwrap(),
                    g : i,
                }
            ).unwrap();

            return 0;
        });

    let output = json::object! 
        {
            matches: all_matches,
            names: names,
        };

    let output_json = json::stringify(output);

    println!("{}", output_json);
    
}
