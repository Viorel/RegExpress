package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"

	coregex "github.com/coregx/coregex"
)

type inputStruct struct {
	Package string
	Pattern string
	Text    string

	EnableDFA               bool
	EnablePrefilter         bool
	EnableASCIIOptimization bool

	MaxDFAStates         *uint32
	DeterminizationLimit *int
	MinLiteralLen        *int
	MaxLiterals          *int
	MaxRecursionDepth    *int

	Posix   bool
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

	is_POSIX := input.Posix
	is_longest := input.Longest
	is_literal := input.Literal

	if is_literal {
		pattern = coregex.QuoteMeta(pattern)
	}

	config := coregex.DefaultConfig()

	config.EnableDFA = input.EnableDFA
	config.EnablePrefilter = input.EnablePrefilter
	config.EnableASCIIOptimization = input.EnableASCIIOptimization

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
