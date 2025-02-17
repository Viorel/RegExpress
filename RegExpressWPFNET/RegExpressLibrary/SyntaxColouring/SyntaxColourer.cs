using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;


namespace RegExpressLibrary.SyntaxColouring
{
    public static class SyntaxColourer
    {
        struct Key
        {
            internal FeatureMatrix fm; // ('struct', to simplify the calculation of hash code)
            internal XLevelEnum xlevel;
            internal bool allow_empty_sets;

            static Key( )
            {
                Debug.Assert( typeof( FeatureMatrix ).IsValueType );
            }
        }

        readonly static Dictionary<Key, Regex> CachedRegexes = new( );

        class ScopeInfo
        {
            internal XLevelEnum XLevel;
        }


        public static void ColourisePattern( ICancellable cnc, ColouredSegments colouredSegments, string pattern, Segment visibleSegment, SyntaxOptions syntaxOptions )
        {
            if( syntaxOptions.Literal ) return;

            FeatureMatrix fm = syntaxOptions.FeatureMatrix;

            Stack<ScopeInfo> scope_stack = new Stack<ScopeInfo>( );
            scope_stack.Push( new ScopeInfo
            {
                XLevel = syntaxOptions.XLevel,
            } );

            int index = 0;
#if DEBUG
            int previous_index = -1;
#endif

            for( ; index < pattern.Length && index <= visibleSegment.End; )
            {
#if DEBUG
                Debug.Assert( previous_index < index );
                previous_index = index;
#endif

                if( cnc.IsCancellationRequested ) return;

                var scope = scope_stack.Peek( );
                var regex = GetCachedRegex( fm, scope.XLevel, syntaxOptions.AllowEmptySets );

                var m = regex.Match( pattern, index );
                if( !m.Success ) break;

                Debug.Assert( m.Length > 0 );
                if( m.Length == 0 ) break;

                Group g;

                if( ( g = m.Groups["lpar"] ).Success )
                {
                    var new_scope = new ScopeInfo { XLevel = scope.XLevel };
                    scope_stack.Push( new_scope );
                }
                // (no 'else')
                if( ( g = m.Groups["rpar"] ).Success )
                {
                    if( scope_stack.Count == 1 )
                    {
                        // unbalanced ')'

                        //.......
                        // TODO: show as error?
                    }
                    else
                    {
                        scope_stack.Pop( );
                    }
                }

                if( ( g = m.Groups["flags"] ).Success )
                {
                    var on = m.Groups["on"].Value;
                    var off = m.Groups["off"].Value;
                    bool circumflex = m.Groups["circumflex"].Success;
                    bool colon = m.Groups["colon"].Success;

                    XLevelEnum? new_xlevel = circumflex ? (XLevelEnum?)XLevelEnum.none : null;

                    if( fm.XXFlag && on.Contains( "xx" ) ) new_xlevel = XLevelEnum.xx;
                    else if( fm.XFlag && on.Contains( 'x' ) ) new_xlevel = XLevelEnum.x;
                    if( ( fm.XFlag || fm.XXFlag ) && off.Contains( 'x' ) ) new_xlevel = XLevelEnum.none; // (including "-xx")

                    if( colon )
                    {
                        var new_scope = new ScopeInfo { XLevel = new_xlevel ?? scope.XLevel };
                        scope_stack.Push( new_scope );
                    }
                    else
                    {
                        if( new_xlevel != null )
                        {
                            scope.XLevel = new_xlevel.Value;
                        }
                    }
                }

                AddCaptures( colouredSegments.Symbols, m.Groups["flags"] );
                AddCaptures( colouredSegments.Symbols, m.Groups["lpar"] );
                AddCaptures( colouredSegments.Symbols, m.Groups["rpar"] );
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.GroupNames, m.Groups["name"] );
                AddCaptures( colouredSegments.CharacterClass, m.Groups["class"] );
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.CharacterEscapes, m.Groups["char_esc"] );
                AddCaptures( colouredSegments.Escapes, m.Groups["escape"] );
                AddCaptures( colouredSegments.Anchors, m.Groups["anchor"] ); //
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.QuotedSequences, m.Groups["qs"] );
                AddCaptures( colouredSegments.Comments, m.Groups["comment"] );
                AddCaptures( colouredSegments.Quantifiers, m.Groups["quant"] );
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.Brackets, m.Groups["lbracket"] );
                AddCaptures( colouredSegments.Brackets, m.Groups["rbracket"] );
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.Symbols, m.Groups["backref"] );
                if( cnc.IsCancellationRequested ) return;
                AddCaptures( colouredSegments.GroupNames, m.Groups["name"] );
                AddCaptures( colouredSegments.Symbols, m.Groups["sym"] );

                index = m.Index + m.Length;
            }

            void AddCaptures( List<Segment> list, Group g )
            {
                if( g.Success )
                {
                    foreach( Capture capture in g.Captures )
                    {
                        var intersection = Segment.Intersection( visibleSegment, capture.Index, capture.Length );
                        if( !intersection.IsEmpty ) list.Add( intersection );
                    }
                }
            }
        }


        public static void HighlightPattern( ICancellable cnc, Highlights highlights, string pattern, int selectionStart, int selectionEnd, Segment visibleSegment,
            SyntaxOptions syntaxOptions )
        {
            if( syntaxOptions.Literal ) return;

            FeatureMatrix fm = syntaxOptions.FeatureMatrix;

            int par_size = fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal ? 1 : fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed ? 2 : 0;
            int bracket_size = 1;

            if( par_size == 0 && bracket_size == 0 ) return;

            List<HighlightHelper.Par> parentheses = new( );
            List<HighlightHelper.Par> brackets = new( );

            Stack<ScopeInfo> scope_stack = new( );
            scope_stack.Push( new ScopeInfo
            {
                XLevel = syntaxOptions.XLevel,
            } );

            int index = 0;
#if DEBUG
            int previous_index = -1;
#endif

            for( ; index < pattern.Length && index <= visibleSegment.End; )
            {
#if DEBUG
                Debug.Assert( previous_index < index );
                previous_index = index;
#endif

                if( cnc.IsCancellationRequested ) return;

                var scope = scope_stack.Peek( );
                var regex = GetCachedRegex( fm, scope.XLevel, syntaxOptions.AllowEmptySets );

                var m = regex.Match( pattern, index );
                if( !m.Success ) break;

                Debug.Assert( m.Length > 0 );
                if( m.Length == 0 ) break;

                Group g;

                if( ( g = m.Groups["lpar"] ).Success )
                {
                    var new_scope = new ScopeInfo { XLevel = scope.XLevel };
                    scope_stack.Push( new_scope );

                    parentheses.Add( new HighlightHelper.Par( g.Index, HighlightHelper.ParKindEnum.Left ) );
                }
                // (no 'else')
                if( ( g = m.Groups["lbracket"] ).Success )
                {
                    foreach( Capture c in g.Captures )
                    {
                        brackets.Add( new HighlightHelper.Par( c.Index, HighlightHelper.ParKindEnum.Left ) );
                    }
                }
                // (both groups are possible)
                if( ( g = m.Groups["rbracket"] ).Success )
                {
                    foreach( Capture c in g.Captures )
                    {
                        brackets.Add( new HighlightHelper.Par( c.Index, HighlightHelper.ParKindEnum.Right ) );
                    }
                }
                if( ( g = m.Groups["rpar"] ).Success )
                {
                    if( scope_stack.Count == 1 )
                    {
                        // unbalanced ')'

                        //.......
                        // TODO: show as error?
                    }
                    else
                    {
                        scope_stack.Pop( );

                        parentheses.Add( new HighlightHelper.Par( g.Index, HighlightHelper.ParKindEnum.Right ) );
                    }
                }

                if( ( g = m.Groups["flags"] ).Success )
                {
                    var on = m.Groups["on"].Value;
                    var off = m.Groups["off"].Value;
                    bool circumflex = m.Groups["circumflex"].Success;
                    bool colon = m.Groups["colon"].Success;

                    XLevelEnum? new_xlevel = circumflex ? (XLevelEnum?)XLevelEnum.none : null;

                    if( fm.XXFlag && on.Contains( "xx" ) ) new_xlevel = XLevelEnum.xx;
                    else if( fm.XFlag && on.Contains( "x" ) ) new_xlevel = XLevelEnum.x;
                    if( ( fm.XFlag || fm.XXFlag ) && off.Contains( "x" ) ) new_xlevel = XLevelEnum.none; // (including "-xx")

                    if( colon )
                    {
                        var new_scope = new ScopeInfo { XLevel = new_xlevel ?? scope.XLevel };
                        scope_stack.Push( new_scope );

                        parentheses.Add( new HighlightHelper.Par( g.Index, HighlightHelper.ParKindEnum.Left ) );
                    }
                    else
                    {
                        if( new_xlevel != null )
                        {
                            scope.XLevel = new_xlevel.Value;
                        }
                    }
                }

                index = m.Index + m.Length;
            }

            HighlightHelper.CommonHighlighting( cnc, highlights, selectionStart, selectionEnd, visibleSegment, parentheses, par_size, brackets, bracket_size );
        }


        static Regex GetCachedRegex( FeatureMatrix fm, XLevelEnum xlevel, bool allow_empty_set )
        {
            Regex? re = null;
            lock( CachedRegexes )
            {
                var key = new Key { fm = fm, xlevel = xlevel, allow_empty_sets = allow_empty_set };

                if( !CachedRegexes.TryGetValue( key, out re ) )
                {
                    re = BuildRegex( fm, xlevel, allow_empty_set );
                    CachedRegexes.Add( key, re );
                }
            }

            return re;
        }


        static Regex BuildRegex( FeatureMatrix fm, XLevelEnum xlevel, bool allowEmptySet )
        {
            bool is_xmode = xlevel == XLevelEnum.x || xlevel == XLevelEnum.xx;

            var pb = new PatternBuilder( );

            var pb_character_escape = new PatternBuilder( );
            pb_character_escape.BeginGroup( "char_esc" );
            {
                if( fm.Esc_oBrace )
                {
                    pb_character_escape.Add( @"\\o(\{[^\}]*\}?)?" ); // octal
                }
                if( fm.Esc_Octal0_1_3 )
                {
                    pb_character_escape.Add( @"\\0[0-7]{0,3}" ); // octal 1-3 digits
                }
                if( fm.Esc_Octal_1_3 )
                {
                    pb_character_escape.Add( @"\\[0-7]{1,3}" ); // octal 1-3 digits
                }
                if( fm.Esc_Octal_2_3 )
                {
                    pb_character_escape.Add( @"\\[0-7]{2,3}" ); // octal 2-3 digits
                }
                if( fm.Esc_xBrace )
                {
                    pb_character_escape.Add( @"\\x\{[^\}]*\}?" ); // hexa
                }
                if( fm.Esc_x2 )
                {
                    pb_character_escape.Add( @"\\x[0-9a-fA-F]{0,2}" ); // hexa, two digits
                }
                if( fm.Esc_uBrace )
                {
                    pb_character_escape.Add( @"\\u\{([^}]+\}?)?" );
                }
                if( fm.Esc_UBrace )
                {
                    pb_character_escape.Add( @"\\U\{([^}]+\}?)?" );
                }
                if( fm.Esc_u4 )
                {
                    pb_character_escape.Add( @"\\u[0-9a-fA-F]{0,4}" ); // hexa, four digits
                }
                if( fm.Esc_U8 )
                {
                    pb_character_escape.Add( @"\\U[0-9a-fA-F]{0,8}" ); // hexa, eight digits
                }
                if( fm.Esc_c1 )
                {
                    pb_character_escape.Add( @"\\c.?" ); // control char
                }
                if( fm.Esc_C1 )
                {
                    pb_character_escape.Add( @"\\C.?" ); // control char
                }
                if( fm.Esc_CMinus )
                {
                    pb_character_escape.Add( @"\\C(-.?)?" ); // control char
                }
                if( fm.Esc_NBrace )
                {
                    if( fm.Class_N )
                    {
                        pb_character_escape.Add( @"\\N\{ [^}]* \}?" ); // symbolic name
                    }
                    else
                    {
                        pb_character_escape.Add( @"\\N(\{ [^}]* \}?)?" ); // symbolic name
                    }
                }

                var chars = String.Concat(
                        fm.Esc_a ? "a" : "",
                        fm.Esc_b ? "b" : "",
                        fm.Esc_e ? "e" : "",
                        fm.Esc_f ? "f" : "",
                        fm.Esc_n ? "n" : "",
                        fm.Esc_r ? "r" : "",
                        fm.Esc_t ? "t" : "",
                        fm.Esc_v ? "v" : ""
                    );
                if( chars.Length > 0 )
                {
                    pb_character_escape.Add( $@"\\[{chars}]" );
                }
            }
            pb_character_escape.EndGroup( );

            var pb_character_class = new PatternBuilder( );
            pb_character_class.BeginGroup( "class" );
            {
                if( fm.Class_pPBrace )
                {
                    if( fm.Class_pP )
                    {
                        pb_character_class.Add( @"\\[pP][^\{]" ); // property, short name
                        pb_character_class.Add( @"\\[pP]( \{ [^}]* \}? )?" ); // property
                    }
                    else
                    {
                        pb_character_class.Add( @"\\[pP] \{ [^}]* \}?" ); // property
                    }
                }
                else
                {
                    if( fm.Class_pP )
                    {
                        pb_character_class.Add( @"\\[pP].?" ); // property, short name
                    }
                }

                if( fm.Class_sSx )
                {
                    pb_character_class.Add( @"\\[sS].?" ); // syntax group
                }

                var chars = string.Concat(
                    fm.Class_Cbyte || fm.Class_Ccp ? "C" : "",
                    fm.Class_dD ? "dD" : "",
                    fm.Class_hHhexa || fm.Class_hHhorspace ? "hH" : "",
                    fm.Class_lL ? "lL" : "",
                    fm.Class_N ? "N" : "",
                    fm.Class_O ? "O" : "",
                    fm.Class_sS ? "sS" : "",
                    fm.Class_uU ? "uU" : "",
                    fm.Class_vV ? "vV" : "",
                    fm.Class_wW ? "wW" : "",
                     fm.Class_R ? "R" : "",
                     fm.Class_X ? "X" : ""
                    );
                if( chars.Length > 0 )
                {
                    pb_character_class.Add( $@"\\[{chars}]" );
                }

                if( fm.Class_Not )
                {
                    pb_character_class.Add( @"\\!(\\?.)?" );
                }
            }
            pb_character_class.EndGroup( );

            var pb_inside_sets = new PatternBuilder( );
            {
                pb_inside_sets.BeginGroup( "char_esc" );
                {
                    if( fm.InsideSets_Esc_oBrace )
                    {
                        pb_inside_sets.Add( @"\\o(\{[^\}]*\}?)?" ); // octal
                    }
                    if( fm.InsideSets_Esc_Octal0_1_3 )
                    {
                        pb_inside_sets.Add( @"\\0[0-7]{0,3}" ); // octal 1-3 digits
                    }
                    if( fm.InsideSets_Esc_Octal_1_3 )
                    {
                        pb_inside_sets.Add( @"\\[0-7]{1,3}" ); // octal 1-3 digits
                    }
                    if( fm.InsideSets_Esc_Octal_2_3 )
                    {
                        pb_inside_sets.Add( @"\\[0-7]{2,3}" ); // octal 2-3 digits
                    }
                    if( fm.InsideSets_Esc_xBrace )
                    {
                        pb_inside_sets.Add( @"\\x\{[^\}]*\}?" ); // hexa
                    }
                    if( fm.InsideSets_Esc_x2 )
                    {
                        pb_inside_sets.Add( @"\\x[0-9a-fA-F]{0,2}" ); // hexa, two digits
                    }
                    if( fm.InsideSets_Esc_uBrace )
                    {
                        pb_inside_sets.Add( @"\\u\{([^}]+\}?)?" );
                    }
                    if( fm.InsideSets_Esc_UBrace )
                    {
                        pb_inside_sets.Add( @"\\U\{([^}]+\}?)?" );
                    }
                    if( fm.InsideSets_Esc_u4 )
                    {
                        pb_inside_sets.Add( @"\\u[0-9a-fA-F]{0,4}" ); // hexa, four digits
                    }
                    if( fm.InsideSets_Esc_U8 )
                    {
                        pb_inside_sets.Add( @"\\U[0-9a-fA-F]{0,8}" ); // hexa, eight digits
                    }
                    if( fm.InsideSets_Esc_c1 )
                    {
                        pb_inside_sets.Add( @"\\c.?" ); // control char
                    }
                    if( fm.InsideSets_Esc_C1 )
                    {
                        pb_inside_sets.Add( @"\\C.?" ); // control char
                    }
                    if( fm.InsideSets_Esc_CMinus )
                    {
                        pb_inside_sets.Add( @"\\C(-.?)?" ); // control char
                    }
                    if( fm.InsideSets_Esc_NBrace )
                    {
                        pb_inside_sets.Add( @"\\N\{ [^}]* \}?" ); // symbolic name
                    }

                    var chars = String.Concat(
                                fm.InsideSets_Esc_a ? "a" : "",
                                fm.InsideSets_Esc_b ? "b" : "",
                                fm.InsideSets_Esc_e ? "e" : "",
                                fm.InsideSets_Esc_f ? "f" : "",
                                fm.InsideSets_Esc_n ? "n" : "",
                                fm.InsideSets_Esc_r ? "r" : "",
                                fm.InsideSets_Esc_t ? "t" : "",
                                fm.InsideSets_Esc_v ? "v" : ""
                            );
                    if( chars.Length > 0 )
                    {
                        pb_inside_sets.Add( String.Format( @"\\[{0}]", chars ) );
                    }
                }
                pb_inside_sets.EndGroup( );

                pb_inside_sets.BeginGroup( "class" );
                {
                    if( fm.InsideSets_Class_pPBrace )
                    {
                        if( fm.InsideSets_Class_pP )
                        {
                            pb_inside_sets.Add( @"\\[pP][^\{]" ); // property, short name
                            pb_inside_sets.Add( @"\\[pP]( \{ [^}]* \}? )?" ); // property
                        }
                        else
                        {
                            pb_inside_sets.Add( @"\\[pP] \{ [^}]* \}?" ); // property
                        }
                    }
                    else
                    {
                        if( fm.InsideSets_Class_pP )
                        {
                            pb_inside_sets.Add( @"\\[pP].?" ); // property, short name
                        }
                    }

                    if( fm.InsideSets_Class_sSx )
                    {
                        pb_inside_sets.Add( @"\\[sS].?" ); // syntax group
                    }

                    var chars = string.Concat(
                        fm.InsideSets_Class_dD ? "dD" : "",
                        fm.InsideSets_Class_hHhexa || fm.InsideSets_Class_hHhorspace ? "hH" : "",
                        fm.InsideSets_Class_lL ? "lL" : "",
                        fm.InsideSets_Class_sS ? "sS" : "",
                        fm.InsideSets_Class_uU ? "uU" : "",
                        fm.InsideSets_Class_vV ? "vV" : "",
                        fm.InsideSets_Class_wW ? "wW" : "",
                         fm.InsideSets_Class_R ? "R" : "",
                         fm.InsideSets_Class_X ? "X" : ""
                        );
                    if( chars.Length > 0 )
                    {
                        pb_inside_sets.Add( $@"\\[{chars}]" );
                    }

                    if( fm.InsideSets_Class_Name )
                    {
                        pb_inside_sets.Add( @"\[: [^:\r\n]* (:\]?)?" );
                    }
                    if( fm.InsideSets_Equivalence )
                    {
                        pb_inside_sets.Add( @"\[= [^=]* = (\] | $)" );
                    }
                    if( fm.InsideSets_Collating )
                    {
                        pb_inside_sets.Add( @"\[\. [^.]* \. (\] | $)" );
                    }
                }
                pb_inside_sets.EndGroup( );

                // -- identity escape
                if( fm.InsideSets_GenericEscape )
                {
                    pb_inside_sets.Add( @"(?<escape>\\.?)" );
                }
            }

            var pb_set_operators = new PatternBuilder( );
            pb_set_operators.BeginGroup( "sym" );
            {
                if( fm.InsideSets_Operator_DoubleAmpersand )
                {
                    pb_set_operators.Add( @"&&" );
                }
                if( fm.InsideSets_Operator_DoubleVerticalLine )
                {
                    pb_set_operators.Add( @"\|\|" );
                }
                if( fm.InsideSets_Operator_DoubleMinus )
                {
                    pb_set_operators.Add( @"--" );
                }
                if( fm.InsideSets_Operator_DoubleTilde )
                {
                    pb_set_operators.Add( @"~~" );
                }
                if( fm.InsideSets_Operator_Ampersand )
                {
                    pb_set_operators.Add( @"&" );
                }
                if( fm.InsideSets_Operator_Plus )
                {
                    pb_set_operators.Add( @"\+" );
                }
                if( fm.InsideSets_Operator_VerticalLine )
                {
                    pb_set_operators.Add( @"\|" );
                }
                if( fm.InsideSets_Operator_Minus )
                {
                    pb_set_operators.Add( @"\-" );
                }
                if( fm.InsideSets_Operator_Circumflex )
                {
                    pb_set_operators.Add( @"\^" );
                }
                if( fm.InsideSets_Operator_Exclamation )
                {
                    pb_set_operators.Add( @"!" );
                }
            }
            pb_set_operators.EndGroup( );

            if( fm.EmptySet && allowEmptySet )
            {
                pb.Add( @"(?<lbracket>\[\^?)(?<rbracket>\])" ); // [], [^]
            }

            if( fm.Class_Name )
            {
                pb.AddGroup( "class", @"\[: [^:\r\n]* (:\]?)?" );
            }

            string class_check = string.Concat(
                fm.InsideSets_Class_Name ? ":" : null,
                fm.InsideSets_Equivalence ? "=" : null,
                fm.InsideSets_Collating ? "." : null
                );
            if( class_check.Length > 0 )
            {
                class_check = $"(?![{class_check}])";
            }

            if( fm.Brackets )
            {
                string comm = fm.InsideSets_XModeComments && is_xmode ? @"((?<comment>\#.*?)(\n|\r|$)) |" : "";
                string qs1 = fm.InsideSets_Literal_QE ? @"(?<qs>\\Q.*?(\\E|$)) |" : "";
                string qs2 = fm.InsideSets_Literal_qBrace ? @"(?<qs>\\q\{ [^}]* \}? ) |" : ""; //TODO: colourise '|' operator: "[\q{xxx|yyy}]"

                if( fm.InsideSets_Operators )
                {
                    // language=regex
                    pb.Add( String.Format( @"
(?<lbracket>\[ \^?)  
\]?
(?>
  (?<lbracket>\[ \^?) {0} (?<c>) \]? | ( {1} {2} {3} {4} | {5} | [^\[\]] )+ | (?<rbracket>\]) (?<-c>)
)*
#(?(c)(?!))
(?<rbracket>\])?", // (use '#(?(c)(?!))' to detect balanced brackets only)
                    class_check,
                    comm,
                    qs1,
                    qs2,
                    pb_set_operators.ToPattern( ),
                    pb_inside_sets.ToPattern( ) ) );
                }
                else
                {
                    // language=regex
                    pb.Add( String.Format( @"(?<lbracket>\[ \^?) \]? ( {0} {1} {2} {3} | [^\]])* (?<rbracket>\])?",
                        comm,
                        qs1,
                        qs2,
                        pb_inside_sets.ToPattern( ) ) ); // [...]
                }
            }

            if( fm.ExtendedBrackets )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new NotSupportedException( );

                if( fm.InsideSets_OperatorsExtended )
                {
                    // language=regex
                    pb.Add( String.Format( @"
(?<lpar>\(\?)(?<lbracket>\[ \^?)  
\]?
(?>
  (?<lbracket>\[ \^?) {0} (?<c>) \]? | ( {1} | {2} | [^\[\]] )+ | (?<rbracket>\]) (?<-c>)
)*
#(?(c)(?!))
((?<rbracket>\])(?<rpar>\))?)?", // (use '#(?(c)(?!))' to detect balanced brackets only)
                    class_check,
                    pb_set_operators.ToPattern( ),
                    pb_inside_sets.ToPattern( ) ) );

                    // TODO: consider '( )' inside '(?[...])' in case of Perl.
                }
                else
                {
                    // language=regex
                    pb.Add( String.Format( @"(?<lpar>\(\?)(?<lbracket>\[ \^?) \]? ({0} | [^\]])* ((?<rbracket>\])(?<rpar>\))?)?", pb_inside_sets.ToPattern( ) ) ); // [...]
                }
            }

            if( fm.InlineComments )
            {
                switch( fm.Parentheses )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    pb.Add( @"(?<comment>\(\?\# [^\)]* \)?)" );
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    pb.Add( @"(?<comment>\\\(\?\# ((?!\\\)).)* (\\\))? )" );
                    break;
                default:
                    throw new InvalidOperationException( );
                }
            }

            if( fm.XModeComments && is_xmode )
            {
                pb.Add( @"(?<comment>\#.*?)(\n|\r|$)" );
            }

            // recursive expressions

            pb.BeginGroup( "sym" );
            {
                switch( fm.Parentheses )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    if( fm.Recursive_Num )
                    {
                        pb.Add( @"\(\? (?<name>\d+) \)?" );
                    }
                    if( fm.Recursive_PlusMinusNum )
                    {
                        pb.Add( @"\(\? (?<name>[+\-]\d+) \)?" );
                    }
                    if( fm.Recursive_R )
                    {
                        pb.Add( @"\(\? R \)?" );
                    }
                    if( fm.Recursive_Name )
                    {
                        pb.Add( @"\(\? & ((?<name>[^)]+) \)?)?" );
                    }
                    if( fm.Recursive_PGtName )
                    {
                        pb.Add( @"\(\? P > ((?<name>[^)]+) \)?)?" );
                    }
                    break;
                default:
                    if( fm.Recursive_Num )
                    {
                        throw new NotSupportedException( );
                    }
                    if( fm.Recursive_PlusMinusNum )
                    {
                        pb.Add( @"\\\(\? (?<name>[+\-]\d+) (\\ \)?)?" );
                    }
                    if( fm.Recursive_R )
                    {
                        throw new NotSupportedException( );
                    }
                    if( fm.Recursive_Name )
                    {
                        throw new NotSupportedException( );
                    }
                    if( fm.Recursive_PGtName )
                    {
                        throw new NotSupportedException( );
                    }
                    break;
                }
            }
            pb.EndGroup( );

            pb.BeginGroup( "backref" );
            {
                if( fm.AllowSpacesInBackref && is_xmode )
                {
                    if( fm.Backref_kApos )
                    {
                        pb.Add( @"\\k \s* ' ((?<name>[^']*) '?)?" );
                    }
                    if( fm.Backref_kLtGt )
                    {
                        pb.Add( @"\\k \s* < ((?<name>[^>]*) >?)?" );
                    }
                    if( fm.Backref_kNum )
                    {
                        pb.Add( @"\\k (?<name>\d+)" );
                    }
                    if( fm.Backref_kNegNum )
                    {
                        pb.Add( @"\\k (?<name>-\d+)" );
                    }
                    if( fm.Backref_Num )
                    {
                        pb.Add( @"\\ (?<name>[1-9]\d*)" );
                    }
                    if( fm.Backref_1_9 )
                    {
                        if( fm.Esc_Octal_2_3 )
                        {
                            pb.Add( @"(?<name>\\[1-9])(?![0-7])" );
                        }
                        else
                        {
                            pb.Add( @"\\(?<name>[1-9])" );
                        }
                    }
                    if( fm.Backref_gApos )
                    {
                        pb.Add( @"\\g \s* ' ((?<name>[^']*) '?)?" );
                    }
                    if( fm.Backref_gLtGt )
                    {
                        pb.Add( @"\\g \s* < ((?<name>[^>]*) >?)?" );
                    }
                    if( fm.Backref_gNum )
                    {
                        pb.Add( @"\\g (?<name>\d+)" );
                    }
                    if( fm.Backref_gNegNum )
                    {
                        pb.Add( @"\\g (?<name>-\d+)" );
                    }
                    if( fm.Backref_gBrace )
                    {
                        pb.Add( @"\\g \s* \{ ((?<name>[^}]*) \}?)? " );
                    }
                    if( fm.Backref_kBrace )
                    {
                        pb.Add( @"\\k \s* \{ ((?<name>[^}]*) \}?)? " );
                    }
                    if( fm.Backref_PEqName )
                    {
                        pb.Add( @"\( \s* \? \s* P \s* = ((?<name>[^)]*) \)?)? " );
                    }
                }
                else
                {
                    if( fm.Backref_kApos )
                    {
                        pb.Add( @"\\k ' ((?<name>[^']*) '?)?" );
                    }
                    if( fm.Backref_kLtGt )
                    {
                        pb.Add( @"\\k < ((?<name>[^>]*) >?)?" );
                    }
                    if( fm.Backref_kNum )
                    {
                        pb.Add( @"\\k (?<name>\d+)" );
                    }
                    if( fm.Backref_kNegNum )
                    {
                        pb.Add( @"\\k (?<name>-\d+)" );
                    }
                    if( fm.Backref_Num )
                    {
                        if( fm.Esc_Octal0_1_3 )
                        {
                            pb.Add( @"\\ (?<name>[1-9]\d*)" );
                        }
                        else
                        {
                            pb.Add( @"\\ (?<name>\d+)" );
                        }
                    }
                    if( fm.Backref_1_9 )
                    {
                        if( fm.Esc_Octal_2_3 )
                        {
                            pb.Add( @"(?<name>\\[1-9])(?![0-7])" );
                        }
                        else
                        {
                            pb.Add( @"\\(?<name>[1-9])" );
                        }
                    }
                    if( fm.Backref_gApos )
                    {
                        pb.Add( @"\\g ' ((?<name>[^']*) '?)?" );
                    }
                    if( fm.Backref_gLtGt )
                    {
                        pb.Add( @"\\g < ((?<name>[^>]*) >?)?" );
                    }
                    if( fm.Backref_gNum )
                    {
                        pb.Add( @"\\g (?<name>\d+)" );
                    }
                    if( fm.Backref_gNegNum )
                    {
                        pb.Add( @"\\g (?<name>-\d+)" );
                    }
                    if( fm.Backref_gBrace )
                    {
                        pb.Add( @"\\g\{ ((?<name>[^}]*) \}?)? " );
                    }
                    if( fm.Backref_kBrace )
                    {
                        pb.Add( @"\\k\{ ((?<name>[^}]*) \}?)? " );
                    }
                    if( fm.Backref_PEqName )
                    {
                        pb.Add( @"\(\? P = ((?<name>[^)]*) \)?)? " );
                    }
                }
            }
            pb.EndGroup( );

            if( fm.Flags )
            {
                switch( fm.Parentheses )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    pb.Add( @"(?<flags>\(\? (?<on>(?!R\))[a-zA-Z01]+)? (?<dash>-)? (?<off>[a-zA-Z01]+)? (\) | $) (?(on)|(?(dash)(?(off)|(?!))|(?!))) )" );
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    pb.Add( @"(?<flags>\\\(\? (?<on>(?!R\))[a-zA-Z]+)? (?<dash>-)? (?<off>[a-zA-Z]+)? (\\\) | $) (?(on)|(?(dash)(?(off)|(?!))|(?!))) )" );
                    break;
                default:
                    throw new InvalidOperationException( );
                }
            }
            if( fm.ScopedFlags )
            {
                switch( fm.Parentheses )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    pb.Add( @"(?<flags>\(\? (?<on>[a-zA-Z]+)? (?<dash>-)? (?<off>[a-zA-Z]+)? ((?<colon>:) | $) (?(on)|(?(dash)|(?(off)|(?!)))) )" );
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    pb.Add( @"(?<flags>\\\(\? (?<on>[a-zA-Z]+)? (?<dash>-)? (?<off>[a-zA-Z]+)? ((?<colon>:) | $) (?(on)|(?(dash)|(?(off)|(?!)))) )" );
                    break;
                default:
                    throw new InvalidOperationException( );
                }
            }

            if( fm.CircumflexFlags )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<flags>\(\? (?<circumflex>\^) (?<on>[a-zA-Z]+)? (?<dash>-)? (?<off>[a-zA-Z]+)? (?<rp>\))? (?(circumflex)|(?(on)|(?(dash)|(?(off)|(?!))))) )" );
            }
            if( fm.ScopedCircumflexFlags )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<flags>\(\? (?<circumflex>\^) (?<on>[a-zA-Z]+)? (?<dash>-)? (?<off>[a-zA-Z]+)? (?<colon>:)? (?(circumflex)|(?(on)|(?(dash)|(?(off)|(?!))))) )" );
            }

            pb.BeginGroup( "lpar" );
            {
                switch( fm.Parentheses )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    if( fm.AllowSpacesInGroups && is_xmode )
                    {
                        if( fm.NoncapturingGroup )
                        {
                            pb.Add( @"\(\s*\?\s*:" );
                        }
                        if( fm.PositiveLookahead )
                        {
                            pb.Add( @"\(\s*\?\s*=" );
                        }
                        if( fm.NegativeLookahead )
                        {
                            pb.Add( @"\(\s*\?\s*!" );
                        }
                        if( fm.PositiveLookbehind )
                        {
                            pb.Add( @"\(\s*\?\s*<\s*=" );
                        }
                        if( fm.NegativeLookbehind )
                        {
                            pb.Add( @"\(\s*\?\s*<\s*!" );
                        }
                        if( fm.AtomicGroup )
                        {
                            pb.Add( @"\(\s*\?\s*>" );
                        }
                        if( fm.BranchReset )
                        {
                            pb.Add( @"\(\s*\?\s*\|" );
                        }
                        if( fm.NonatomicPositiveLookahead )
                        {
                            pb.Add( @"\(\s*\?\s*\*" );
                        }
                        if( fm.NonatomicPositiveLookbehind )
                        {
                            pb.Add( @"\(\s*\?\s*<\s*\*" );
                        }
                        if( fm.AbsentOperator )
                        {
                            pb.Add( @"\(\s*\?\s*~" );
                        }
                        if( fm.NamedGroup_Apos )
                        {
                            pb.Add( @"\(\s*\?\s* ' ((?<name>[^']*) '?)?" );
                        }
                        if( fm.NamedGroup_LtGt )
                        {
                            pb.Add( @"\(\s*\?\s* < ((?<name>[^>]*) >?)?" );
                        }
                        if( fm.NamedGroup_PLtGt )
                        {
                            pb.Add( @"\(\s*\?\s* P \s* < ((?<name>[^>]*) >?)?" );
                        }
                    }
                    else
                    {
                        if( fm.NoncapturingGroup )
                        {
                            pb.Add( @"\(\?:" );
                        }
                        if( fm.PositiveLookahead )
                        {
                            pb.Add( @"\(\?=" );
                        }
                        if( fm.NegativeLookahead )
                        {
                            pb.Add( @"\(\?!" );
                        }
                        if( fm.PositiveLookbehind )
                        {
                            pb.Add( @"\(\?<=" );
                        }
                        if( fm.NegativeLookbehind )
                        {
                            pb.Add( @"\(\?<!" );
                        }
                        if( fm.AtomicGroup )
                        {
                            pb.Add( @"\(\?>" );
                        }
                        if( fm.BranchReset )
                        {
                            pb.Add( @"\(\?\|" );
                        }
                        if( fm.NonatomicPositiveLookahead )
                        {
                            pb.Add( @"\(\?\*" );
                        }
                        if( fm.NonatomicPositiveLookbehind )
                        {
                            pb.Add( @"\(\?<\*" );
                        }
                        if( fm.AbsentOperator )
                        {
                            pb.Add( @"\(\?~" );
                        }
                        if( fm.NamedGroup_Apos )
                        {
                            pb.Add( @"\(\? ' ((?<name>[^']*) '?)?" );
                        }
                        if( fm.NamedGroup_LtGt )
                        {
                            pb.Add( @"\(\? < ((?<name>[^>]*) >?)?" );
                        }
                        if( fm.NamedGroup_PLtGt )
                        {
                            pb.Add( @"\(\? P < ((?<name>[^>]*) >?)?" );
                        }
                        if( fm.NamedGroup_AtApos )
                        {
                            pb.Add( @"\(\? @ ' ((?<name>[^']*) '?)?" );
                        }
                        if( fm.NamedGroup_AtLtGt )
                        {
                            pb.Add( @"\(\? @ < ((?<name>[^>]*) >?)?" );
                        }
                        if( fm.CapturingGroup )
                        {
                            pb.Add( @"\(\? @" );
                        }
                    }
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    if( fm.AllowSpacesInGroups && is_xmode )
                    {
                        throw new NotSupportedException( );
                    }
                    else
                    {
                        if( fm.NoncapturingGroup )
                        {
                            pb.Add( @"\\\(\?:" );
                        }
                        if( fm.PositiveLookahead )
                        {
                            pb.Add( @"\\\(\?=" );
                        }
                        if( fm.NegativeLookahead )
                        {
                            pb.Add( @"\\\(\?!" );
                        }
                        if( fm.PositiveLookbehind )
                        {
                            pb.Add( @"\\\(\?<=" );
                        }
                        if( fm.NegativeLookbehind )
                        {
                            pb.Add( @"\\\(\?<!" );
                        }
                        if( fm.AtomicGroup )
                        {
                            pb.Add( @"\\\(\?>" );
                        }
                        if( fm.BranchReset )
                        {
                            pb.Add( @"\\\(\?\|" );
                        }
                        if( fm.NonatomicPositiveLookahead )
                        {
                            pb.Add( @"\\\(\?\*" );
                        }
                        if( fm.NonatomicPositiveLookbehind )
                        {
                            pb.Add( @"\\\(\?<\*" );
                        }
                        if( fm.AbsentOperator )
                        {
                            pb.Add( @"\\\(\?~" );
                        }
                        if( fm.NamedGroup_Apos )
                        {
                            pb.Add( @"\\\(\? ' ((?<name>[^']*) '?)?" );
                        }
                        if( fm.NamedGroup_LtGt )
                        {
                            pb.Add( @"\\\(\? < ((?<name>[^>]*) >?)?" );
                        }
                        if( fm.NamedGroup_PLtGt )
                        {
                            pb.Add( @"\\\(\? P < ((?<name>[^>]*) >?)?" );
                        }
                    }
                    break;
                default:
                    if( fm.NoncapturingGroup )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.PositiveLookahead )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NegativeLookahead )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.PositiveLookbehind )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NegativeLookbehind )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.AtomicGroup )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.BranchReset )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NonatomicPositiveLookahead )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NonatomicPositiveLookbehind )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.AbsentOperator )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NamedGroup_Apos )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NamedGroup_LtGt )
                    {
                        throw new InvalidOperationException( );
                    }
                    if( fm.NamedGroup_PLtGt )
                    {
                        throw new InvalidOperationException( );
                    }
                    break;
                }
            }
            pb.EndGroup( );

            if( fm.Conditional_BackrefByNumber )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( (?<name>[+\-]?\d+) \) )" );
            }
            if( fm.Conditional_BackrefByName )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( (?![<']) (?!R&) (?!VERSION [>=]) (?<name>[^)])+ \) )" );
            }
            if( fm.Conditional_PatternOrBackrefByName )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?) (?=\()" );
            }
            if( fm.Conditional_BackrefByName_Apos )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( ' ((?<name>[^']+) (' \)?)?)? )" );
            }
            if( fm.Conditional_BackrefByName_LtGt )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( < ((?<name>[^>]+) (> \)?)?)? )" );
            }
            if( fm.Conditional_RName )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( R& ((?<name>[^)]+) \)?)? )" );
            }
            if( fm.Conditional_R )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( R ((?<name>\d*) \)?)? )" );
            }
            if( fm.Conditional_DEFINE )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( DEFINE \)? )" );
            }
            if( fm.Conditional_VERSION )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?\( VERSION >? (= (\d+(\.\d+)? \)?)?)? )" );
            }
            if( fm.Conditional_Pattern )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar>\(\?) (?=\()" );
            }

            if( fm.ControlVerbs )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<sym>\(\* (PRUNE | SKIP | MARK | THEN | COMMIT | FAIL | F | ACCEPT | UTF8? | UCP ) ((: [^)]+)? \)?)? )" );
                pb.Add( @"(?<sym>\(\*: ([^)]+ \)?)? )" );
            }

            if( fm.ScriptRuns )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.Add( @"(?<lpar> \(\* [^:]+ :? )" );
            }

            if( fm.EmptyConstruct )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.AddGroup( "sym", @"\(\?\)" );
            }

            if( fm.EmptyConstructX && is_xmode )
            {
                if( fm.Parentheses != FeatureMatrix.PunctuationEnum.Normal ) throw new InvalidOperationException( );

                pb.AddGroup( "sym", @"\(\? \s+ \)" );
            }

            pb.BeginGroup( "quant" );
            {
                if( fm.Quantifier_Asterisk )
                {
                    pb.Add( @"\*" );
                }

                switch( fm.Quantifier_Plus )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    pb.Add( @"\+" );
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    pb.Add( @"\\\+" );
                    break;
                }

                switch( fm.Quantifier_Question )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    pb.Add( @"\?" );
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    pb.Add( @"\\\?" );
                    break;
                }

                switch( fm.Quantifier_Braces )
                {
                case FeatureMatrix.PunctuationEnum.Normal:
                    if( fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.Both || ( fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.XModeOnly && is_xmode ) )
                    {
                        pb.Add( @"\{ \s* \d+ (\s*,\s* \d*)? \s* \}" ); // (if does not match, then it is not a quantifier)
                        if( fm.Quantifier_LowAbbrev )
                        {
                            pb.Add( @"\{ \s*,\s* \d+ \s* \}" );
                        }
                    }
                    else
                    {
                        pb.Add( @"\{ \d+(,\d*)? \}" ); // (if does not match, then it is not a quantifier)
                        if( fm.Quantifier_LowAbbrev )
                        {
                            pb.Add( @"\{ ,\d+ \}" );
                        }
                    }
                    break;
                case FeatureMatrix.PunctuationEnum.Backslashed:
                    if( fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.Both || ( fm.Quantifier_Braces_Spaces == FeatureMatrix.SpaceUsage.XModeOnly && is_xmode ) )
                    {
                        pb.Add( @"\\\{ \s* \d+(\s*,\s* \d*)? \s* \\\}?" );
                        if( fm.Quantifier_LowAbbrev )
                        {
                            pb.Add( @"\\\{ \s*,\s* \d+ \s* \\\}" );
                        }
                    }
                    else
                    {
                        pb.Add( @"\\\{ \d+(,\d*)? \\\}?" );
                        if( fm.Quantifier_LowAbbrev )
                        {
                            pb.Add( @"\\\{ ,\d+ \\\}" );
                        }
                    }
                    break;
                }
            }
            pb.EndGroup( );

            if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Normal )
            {
                pb.Add( @"(?<lpar>\()" );
                pb.Add( @"(?<rpar>\))" );
            }

            if( fm.Parentheses == FeatureMatrix.PunctuationEnum.Backslashed )
            {
                pb.Add( @"(?<lpar>\\\()" );
                pb.Add( @"(?<rpar>\\\))" );
            }

            // anchors, bounds
            pb.BeginGroup( "anchor" );
            {
                if( fm.Anchor_Circumflex )
                {
                    pb.Add( @"\^" );
                }
                if( fm.Anchor_Dollar )
                {
                    pb.Add( @"\$" );
                }
                if( fm.Anchor_A )
                {
                    pb.Add( @"\\A" );
                }
                if( fm.Anchor_Z )
                {
                    pb.Add( @"\\Z" );
                }
                if( fm.Anchor_z )
                {
                    pb.Add( @"\\z" );
                }
                if( fm.Anchor_G )
                {
                    pb.Add( @"\\G" );
                }
                if( fm.Anchor_bg )
                {
                    pb.Add( @"\\b\{(g\}?)?" );
                }
                if( fm.Anchor_bBBrace )
                {
                    pb.Add( @"\\[bB] (\{ ( [^}]+ \}?)?)?" );
                }
                if( fm.Anchor_bB )
                {
                    pb.Add( @"\\[bB]" );
                }
                if( fm.Anchor_K )
                {
                    pb.Add( @"\\K" );
                }
                if( fm.Anchor_mM )
                {
                    pb.Add( @"\\[mM]" );
                }
                if( fm.Anchor_LtGt )
                {
                    pb.Add( @"\\[<>]" );
                }
                if( fm.Anchor_GraveApos )
                {
                    pb.Add( @"\\[`']" );
                }
                if( fm.Anchor_yY )
                {
                    pb.Add( @"\\[yY]" );
                }
            }
            pb.EndGroup( );

            if( fm.Class_Dot )
            {
                pb.Add( @"(?<class>\.)" );
            }

            if( fm.VerticalLine == FeatureMatrix.PunctuationEnum.Normal )
            {
                pb.Add( @"(?<sym>\|)" );
            }
            if( fm.VerticalLine == FeatureMatrix.PunctuationEnum.Backslashed )
            {
                pb.Add( @"(?<sym>\\\|)" );
            }

            if( fm.Literal_QE )
            {
                pb.Add( @"(?<qs>\\Q .*? (\\E | $))" );
            }

            pb.Add( pb_character_escape );
            pb.Add( pb_character_class );

            // -- identity escape
            if( fm.GenericEscape )
            {
                pb.Add( @"(?<escape>\\.?)" ); // generic'\...'
            }


            // TODO: colourise partial constructs; for example: "(?"

            return pb.ToRegex( );
        }

    }
}
