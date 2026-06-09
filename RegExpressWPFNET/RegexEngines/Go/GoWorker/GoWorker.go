package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"

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
	Flags   string
}

type outputStruct struct {
	Names   []string
	Matches [][]int
}

func matchRegexp(output *outputStruct, pattern string, text string, flags string) {

	var err error

	is_POSIX := strings.Contains(flags, "P")
	is_longest := strings.Contains(flags, "L")
	is_literal := strings.Contains(flags, "Q")

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

func matchRegexp2(output *outputStruct, pattern string, text string, flags string) {

	var err error

	var options regexp2.RegexOptions = regexp2.None

	if strings.Contains(flags, "i") {
		options |= regexp2.IgnoreCase
	}
	if strings.Contains(flags, "m") {
		options |= regexp2.Multiline
	}
	if strings.Contains(flags, "n") {
		options |= regexp2.ExplicitCapture
	}
	if strings.Contains(flags, "s") {
		options |= regexp2.Singleline
	}
	if strings.Contains(flags, "x") {
		options |= regexp2.IgnorePatternWhitespace
	}
	if strings.Contains(flags, "r") {
		options |= regexp2.RightToLeft
	}
	if strings.Contains(flags, "e") {
		options |= regexp2.ECMAScript
	}
	if strings.Contains(flags, "2") {
		options |= regexp2.RE2
	}
	if strings.Contains(flags, "u") {
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

func matchRexa(output *outputStruct, pattern string, text string, flags string) {

	var err error

	is_literal := strings.Contains(flags, "Q")
	is_longest := strings.Contains(flags, "L")

	if is_literal {
		pattern = rexa.QuoteMeta(pattern)
	}

	options := rexa.CompileOptions{}

	if strings.Contains(flags, "i") {
		options.Flags |= rexaSyntax.FlagCaseInsensitive
	}
	if strings.Contains(flags, "m") {
		options.Flags |= rexaSyntax.FlagMultiline
	}
	if strings.Contains(flags, "s") {
		options.Flags |= rexaSyntax.FlagDotAll
	}
	if strings.Contains(flags, "U") {
		options.Flags |= rexaSyntax.FlagUngreedy
	}
	if strings.Contains(flags, "u") {
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

func matchCoregex(output *outputStruct, pattern string, text string, flags string) {

	var err error

	is_POSIX := strings.Contains(flags, "P")
	is_longest := strings.Contains(flags, "L")
	is_literal := strings.Contains(flags, "Q")

	if is_literal {
		pattern = coregex.QuoteMeta(pattern)
	}

	var re *coregex.Regexp

	if is_POSIX {
		re, err = coregex.CompilePOSIX(pattern)
	} else {
		re, err = coregex.Compile(pattern)
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

	//fmt.Println("Input: ", input_text)

	var input inputStruct

	err = json.Unmarshal([]byte(input_text), &input)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	//fmt.Printf( "Pattern: '%s'\n", input.Pattern)
	//fmt.Printf( "Text: '%s'\n", input.Text)

	package0 := input.Package
	pattern := input.Pattern
	text := input.Text
	flags := input.Flags

	output := &outputStruct{}

	switch package0 {
	case "regexp":

		matchRegexp(output, pattern, text, flags)

	case "regexp2":

		matchRegexp2(output, pattern, text, flags)

	case "rexa":

		matchRexa(output, pattern, text, flags)

	case "coregex":

		matchCoregex(output, pattern, text, flags)

	default:
		fmt.Fprintf(os.Stderr, "Invalid package: '%s'\n", package0)

		return
	}

	output_json, err := json.Marshal(output)

	if err != nil {
		fmt.Fprintln(os.Stderr, "Error: ", err)

		return
	}

	fmt.Printf("%s\n", output_json)
}
