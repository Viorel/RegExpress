package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"

	"regexp"

	regexp2 "github.com/dlclark/regexp2/v2"
	regexp2compat "github.com/dlclark/regexp2/v2/compat"
	rexa "github.com/himclix/rexa"
	"github.com/himclix/rexa/syntax"
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

	is_POSIX := strings.Contains(input.Flags, "P")
	is_longest := strings.Contains(input.Flags, "L")
	is_literal := strings.Contains(input.Flags, "Q")

	output := &outputStruct{}

	switch package0 {
	case "regexp":
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

	case "regexp2":
		if is_literal {
			pattern = regexp.QuoteMeta(pattern)
		}

		var options regexp2.RegexOptions = regexp2.None

		if strings.Contains(input.Flags, "i") {
			options |= regexp2.IgnoreCase
		}
		if strings.Contains(input.Flags, "m") {
			options |= regexp2.Multiline
		}
		if strings.Contains(input.Flags, "n") {
			options |= regexp2.ExplicitCapture
		}
		if strings.Contains(input.Flags, "s") {
			options |= regexp2.Singleline
		}
		if strings.Contains(input.Flags, "x") {
			options |= regexp2.IgnorePatternWhitespace
		}
		if strings.Contains(input.Flags, "r") {
			options |= regexp2.RightToLeft
		}
		if strings.Contains(input.Flags, "e") {
			options |= regexp2.ECMAScript
		}
		if strings.Contains(input.Flags, "2") {
			options |= regexp2.RE2
		}
		if strings.Contains(input.Flags, "u") {
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

	case "rexa":
		if is_literal {
			pattern = rexa.QuoteMeta(pattern)
		}

		options := rexa.CompileOptions{}

		if strings.Contains(input.Flags, "i") {
			options.Flags |= syntax.FlagCaseInsensitive
		}
		if strings.Contains(input.Flags, "m") {
			options.Flags |= syntax.FlagMultiline
		}
		if strings.Contains(input.Flags, "s") {
			options.Flags |= syntax.FlagDotAll
		}
		if strings.Contains(input.Flags, "U") {
			options.Flags |= syntax.FlagUngreedy
		}
		if strings.Contains(input.Flags, "u") {
			options.Flags |= syntax.FlagUnicode
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
