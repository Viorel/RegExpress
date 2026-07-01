package main

import (
    "bufio"
    "encoding/json"
    "fmt"
    "io"
    "os"

    regexp "regexp"

    regexp2 "github.com/dlclark/regexp2/v2"
    regexp2compat "github.com/dlclark/regexp2/v2/compat"
    rexa "github.com/himclix/rexa"
    rexaSyntax "github.com/himclix/rexa/syntax"

    coregex "github.com/coregx/coregex"
)

type inputStruct struct {
    Package string
    Pattern string
    Text    string

    Posix_syntax  bool
    Longest_match bool
    Literal       bool

    IgnoreCase              bool
    Multiline               bool
    ExplicitCapture         bool
    Singleline              bool
    IgnorePatternWhitespace bool
    RightToLeft             bool
    ECMAScript              bool
    RE2                     bool
    Unicode                 bool

    Ungreedy bool

    // coregex
    EnableDFA               *bool
    EnablePrefilter         *bool
    MaxDFAStates            *uint32
    DeterminizationLimit    *int
    MinLiteralLen           *int
    MaxLiterals             *int
    MaxRecursionDepth       *int
    EnableASCIIOptimization *bool
}

type outputStruct struct {
    Names   []string
    Matches [][]int
}

func matchRegexp(output *outputStruct, input inputStruct) {

    var err error

    pattern := input.Pattern
    text := input.Text

    is_POSIX := input.Posix_syntax
    is_longest := input.Longest_match
    is_literal := input.Literal

    if is_literal {
        pattern = regexp.QuoteMeta(pattern)
    }

    var re *regexp.Regexp

    if is_POSIX {
        re, err = regexp.CompilePOSIX(pattern)
    } else {
        re, err = regexp.Compile(pattern)
    }

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    if is_longest {
        re.Longest()
    }

    names := re.SubexpNames() // []string
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex(text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output.Names = names
    output.Matches = matches

    //fmt.Printf( "output: %+v\n", output)
}

func matchRegexp2(output *outputStruct, input inputStruct) {

    var err error

    pattern := input.Pattern
    text := input.Text

    var options regexp2.RegexOptions = regexp2.None

    if input.IgnoreCase {
        options |= regexp2.IgnoreCase
    }
    if input.Multiline {
        options |= regexp2.Multiline
    }
    if input.ExplicitCapture {
        options |= regexp2.ExplicitCapture
    }
    if input.Singleline {
        options |= regexp2.Singleline
    }
    if input.IgnorePatternWhitespace {
        options |= regexp2.IgnorePatternWhitespace
    }
    if input.RightToLeft {
        options |= regexp2.RightToLeft
    }
    if input.ECMAScript {
        options |= regexp2.ECMAScript
    }
    if input.RE2 {
        options |= regexp2.RE2
    }
    if input.Unicode {
        options |= regexp2.Unicode
    }

    var re *regexp2compat.Regexp

    re, err = regexp2compat.Compile(pattern, regexp2.OptionMaintainCaptureOrder(), options)

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    names := re.Unwrap().GetGroupNames() // (it puts numbers instead of empty or null strings)
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex(text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output.Names = names
    output.Matches = matches

    //fmt.Printf( "output: %+v\n", output)
}

func matchRexa(output *outputStruct, input inputStruct) {

    var err error

    pattern := input.Pattern
    text := input.Text

    is_literal := input.Literal
    is_longest := input.Longest_match

    if is_literal {
        pattern = rexa.QuoteMeta(pattern)
    }

    options := rexa.CompileOptions{}

    if input.IgnoreCase {
        options.Flags |= rexaSyntax.FlagCaseInsensitive
    }
    if input.Multiline {
        options.Flags |= rexaSyntax.FlagMultiline
    }
    if input.Singleline {
        options.Flags |= rexaSyntax.FlagDotAll
    }
    if input.Ungreedy {
        options.Flags |= rexaSyntax.FlagUngreedy
    }
    if input.Unicode {
        options.Flags |= rexaSyntax.FlagUnicode
    }

    var re *rexa.Regexp

    re, err = rexa.CompileWithOptions(pattern, options)

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    if is_longest {
        re.Longest() // TODO: not supported?
    }

    names := re.SubexpNames() // []string
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex(text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output.Names = names
    output.Matches = matches

    //fmt.Printf( "output: %+v\n", output)
}

func matchCoregex(output *outputStruct, input inputStruct) {

    var err error

    pattern := input.Pattern
    text := input.Text

    is_POSIX := input.Posix_syntax
    is_longest := input.Longest_match
    is_literal := input.Literal

    if is_literal {
        pattern = coregex.QuoteMeta(pattern)
    }

    config := coregex.DefaultConfig()

    if input.EnableDFA != nil {
        config.EnableDFA = *input.EnableDFA
    }
    if input.EnablePrefilter != nil {
        config.EnablePrefilter = *input.EnablePrefilter
    }
    if input.EnableASCIIOptimization != nil {
        config.EnableASCIIOptimization = *input.EnableASCIIOptimization
    }
    if input.MaxDFAStates != nil {
        config.MaxDFAStates = *input.MaxDFAStates
    }
    if input.DeterminizationLimit != nil {
        config.DeterminizationLimit = *input.DeterminizationLimit
    }
    if input.MinLiteralLen != nil {
        config.MinLiteralLen = *input.MinLiteralLen
    }
    if input.MaxLiterals != nil {
        config.MaxLiterals = *input.MaxLiterals
    }
    if input.MaxRecursionDepth != nil {
        config.MaxRecursionDepth = *input.MaxRecursionDepth
    }

    var re *coregex.Regexp

    if is_POSIX {
        re, err = coregex.CompilePOSIX(pattern)
    } else {
        re, err = coregex.CompileWithConfig(pattern, config)
    }

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    if is_longest {
        re.Longest()
    }

    names := re.SubexpNames() // []string
    //fmt.Printf( "names: %q\n", names)

    matches := re.FindAllStringSubmatchIndex(text, -1) // [][]int
    //fmt.Printf( "matches: %d\n", matches)

    output.Names = names
    output.Matches = matches

    //fmt.Printf( "output: %+v\n", output)
}

func main() {
    var err error

    reader := bufio.NewReader(os.Stdin)
    input_text, err := reader.ReadString(0)
    if err != nil && err != io.EOF {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    //fmt.Printf("Input text: {%s}\n", input_text)

    var input inputStruct

    err = json.Unmarshal([]byte(input_text), &input)
    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    //fmt.Printf("Input struct: {%+v}\n", input)

    package0 := input.Package

    output := &outputStruct{}

    switch package0 {
    case "regexp":

        matchRegexp(output, input)

    case "regexp2":

        matchRegexp2(output, input)

    case "rexa":

        matchRexa(output, input)

    case "coregex":

        matchCoregex(output, input)

    default:
        fmt.Fprintf(os.Stderr, "Invalid package: '%s'\n", package0)

        return
    }

    output_json, err := json.Marshal(output)

    if err != nil {
        fmt.Fprintln(os.Stderr, err)

        return
    }

    fmt.Printf("%s\n", output_json)
}
