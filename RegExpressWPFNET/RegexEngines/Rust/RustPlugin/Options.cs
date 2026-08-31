using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace RustPlugin
{
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

    internal class Options
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

        public Options Clone( )
        {
            return (Options)MemberwiseClone( );
        }
    }
}
