package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"regexp"

	regexp2 "github.com/dlclark/regexp2/v2"
	regexp2compat "github.com/dlclark/regexp2/v2/compat"
	rexa "github.com/himclix/rexa"
)

type Input struct {
	Package string
	Pattern string
	Text    string
	Flags   string
}

type Output struct {
	Names   []string
	Matches [][]int
}

func main() {
	reader := bufio.NewReader(os.Stdin)
	input_text, _ := reader.ReadString(0)

	//fmt.Println("Input: ", input_text)

	var input Input
	var err error

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

	output := &Output{}

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

		var re *regexp2compat.Regexp

		re, err = regexp2compat.Compile(pattern, regexp2.OptionMaintainCaptureOrder())

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

		var re *rexa.Regexp

		re, err = rexa.Compile(pattern)

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
