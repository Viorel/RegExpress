package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"

	regexp2 "github.com/dlclark/regexp2/v2"
	regexp2compat "github.com/dlclark/regexp2/v2/compat"
)

type inputStruct struct {
	Pattern string
	Text    string

	IgnoreCase              bool
	Multiline               bool
	ExplicitCapture         bool
	Singleline              bool
	IgnorePatternWhitespace bool
	RightToLeft             bool
	ECMAScript              bool
	RE2                     bool
	Unicode                 bool
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

	//fmt.Printf("Input text: {%s}\n", input_text)

	var input inputStruct

	err = json.Unmarshal([]byte(input_text), &input)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	//fmt.Printf("Input struct: {%+v}\n", input)

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

	var output outputStruct

	output.Names = names
	output.Matches = matches

	//fmt.Printf( "output: %+v\n", output)

	output_json, err := json.Marshal(output)

	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	fmt.Printf("%s\n", output_json)
}
