package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"

	rexa "github.com/himclix/rexa"
	rexaSyntax "github.com/himclix/rexa/syntax"
)

type inputStruct struct {
	Pattern string
	Text    string

	FlagCaseInsensitive bool
	FlagMultiline       bool
	FlagDotAll          bool
	FlagUngreedy        bool
	FlagUnicode         bool

	Longest bool
	Literal bool
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

	is_literal := input.Literal
	is_longest := input.Longest

	if is_literal {
		pattern = rexa.QuoteMeta(pattern)
	}

	options := rexa.CompileOptions{}

	if input.FlagCaseInsensitive {
		options.Flags |= rexaSyntax.FlagCaseInsensitive
	}
	if input.FlagMultiline {
		options.Flags |= rexaSyntax.FlagMultiline
	}
	if input.FlagDotAll {
		options.Flags |= rexaSyntax.FlagDotAll
	}
	if input.FlagUngreedy {
		options.Flags |= rexaSyntax.FlagUngreedy
	}
	if input.FlagUnicode {
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
