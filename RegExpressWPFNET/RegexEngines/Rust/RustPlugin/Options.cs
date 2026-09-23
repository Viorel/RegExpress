namespace RustPlugin;

enum CrateEnum
{
    None,
    regex,
    regex_lite,
    fancy_regex,
    regress,
    resharp,
    anre,
    real_regex,
    java_regex,
    regexr,
    rexile,
    iregexp_rs,
    ferroni,
    rusty_expressions,
    derivre,
}

enum UnicodeModeEnum // ('resharp')
{
    None,
    Default,
    Ascii,
    Full,
    Javascript,
}

enum BytesModeEnum // ('fancy')
{
    None,
    Default,
    Unicode,
    Ascii,
    UnicodeBytes,
}

enum MatchModeEnum // ('iregex-rs')
{
    None,
    Full,
    Search,
}

enum OnigSyntaxTypeEnum // ('ferroni')
{
    None,
    OnigSyntaxASIS,
    OnigSyntaxEmacs,
    OnigSyntaxGnuRegex,
    OnigSyntaxGrep,
    OnigSyntaxJava,
    OnigSyntaxOniguruma,
    OnigSyntaxPerl,
    OnigSyntaxPerl_NG,
    OnigSyntaxPosixBasic,
    OnigSyntaxPosixExtended,
    OnigSyntaxPython,
    OnigSyntaxRuby,
}

class Options
{
    public CrateEnum @crate { get; set; } = CrateEnum.regex;
    public bool UseBuilder { get; set; } = false;

    public bool case_insensitive { get; set; }
    public bool multi_line { get; set; }
    public bool dot_matches_new_line { get; set; }
    public bool swap_greed { get; set; }
    public bool ignore_whitespace { get; set; }
    public bool unicode { get; set; } = true;
    public bool octal { get; set; }
    public bool crlf { get; set; } // ('regex', 'regex-lite', 'fancy-regex' ((?R) flag))
    public bool no_opt { get; set; } // ('regress')
    public bool unicode_sets { get; set; } // ('regress', 'java_regex')
    public bool oniguruma_mode { get; set; } // ('fancy-regex')
    public bool find_not_empty { get; set; } // ('fancy-regex')
    public bool ignore_numbered_groups_when_named_groups_exist { get; set; } // ('fancy-regex')
    public bool seek { get; set; } // ('fancy-regex')
    public bool disallow_empty_match_at_eof_after_newline { get; set; } // ('fancy-regex')
    public bool allow_input_assertion_overrides { get; set; } // ('fancy-regex')
    public bool start_text { get; set; } // ('fancy-regex')
    public bool end_text { get; set; } // ('fancy-regex')
    public bool hardened { get; set; } // ('resharp')
    public bool unbounded_size { get; set; } // ('resharp')
    public UnicodeModeEnum UnicodeMode { get; set; } = UnicodeModeEnum.Default; // ('resharp')

    // Regex and Regex-lite

    public string? size_limit { get; set; } // also for 'regexr'
    public string? dfa_size_limit { get; set; } // (not in 'regex_lite')
    public string? nest_limit { get; set; } // also for 'regexr'

    // Fancy-regex

    public bool leftmost_longest { get; set; }
    public BytesModeEnum bytes_mode { get; set; } = BytesModeEnum.Default;
    public string? backtrack_limit { get; set; } // also for 'regexr'
    public string? delegate_size_limit { get; set; }
    public string? delegate_dfa_size_limit { get; set; }

    // Resharp

    public string? max_dfa_capacity { get; set; }
    public string? lookahead_context_max { get; set; }

    // Regex-anre

    public bool anre_syntax { get; set; }

    // Real-regex

    public bool fallback { get; set; }

    // Java_regex

    public bool d { get; set; }
    public bool l { get; set; }

    // Regexr

    public bool jit { get; set; }
    public bool optimize_prefixes { get; set; }

    // IRegexpRs

    public MatchModeEnum MatchMode { get; set; } = MatchModeEnum.Full;

    // Ferrony

    public OnigSyntaxTypeEnum OnigSyntaxType { get; set; } = OnigSyntaxTypeEnum.OnigSyntaxOniguruma; // (also 'RustyExpressions')

    // RustyExpressions

    public bool NEGATE_SINGLELINE { get; set; }
    public bool DONT_CAPTURE_GROUP { get; set; }
    public bool CAPTURE_GROUP { get; set; }
    public bool NOTBOL { get; set; }
    public bool NOTEOL { get; set; }
    public bool IGNORECASE_IS_ASCII { get; set; }
    public bool WORD_IS_ASCII { get; set; }
    public bool DIGIT_IS_ASCII { get; set; }
    public bool SPACE_IS_ASCII { get; set; }
    public bool POSIX_IS_ASCII { get; set; }
    public bool TEXT_SEGMENT_EXTENDED_GRAPHEME_CLUSTER { get; set; }
    public bool TEXT_SEGMENT_WORD { get; set; }
    public bool NOT_BEGIN_STRING { get; set; }
    public bool NOT_END_STRING { get; set; }
    public bool NOT_BEGIN_POSITION { get; set; }
    public bool CALLBACK_EACH_MATCH { get; set; }
    public bool MATCH_WHOLE_STRING { get; set; }

    public string? stack_limit { get; set; }
    public string? retry_limit_in_match { get; set; }
    public string? retry_limit_in_search { get; set; }
    public string? subexp_call_limit { get; set; }

    // Derivre

    //

    public Options Clone( )
    {
        return (Options)MemberwiseClone( );
    }
}
