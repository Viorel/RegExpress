package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"

	"github.com/go-embedded-r/tre"
)

type inputStruct struct {
	Pattern string
	Text    string

	Options *inputOptions
}

type inputOptions struct {
	Extended   bool
	ICase      bool
	Newline    bool
	NoSub      bool
	Literal    bool
	RightAssoc bool
	Ungreedy   bool
	//UseBytes   bool

	NotBOL              bool
	NotEOL              bool
	BacktrackingMatcher bool

	FindAll bool
}

type outputStruct struct {
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

	var opt tre.Options
	var eflags tre.EFlags = 0
	var findAll = false
	var no_sub = false

	if input.Options != nil {

		o := input.Options

		if o.Extended {
			opt.Flags |= tre.Extended
		}
		if o.ICase {
			opt.Flags |= tre.ICase
		}
		if o.Newline {
			opt.Flags |= tre.Newline
		}
		if o.NoSub {
			opt.Flags |= tre.NoSub
		}
		if o.Literal {
			opt.Flags |= tre.Literal
		}
		if o.RightAssoc {
			opt.Flags |= tre.RightAssoc
		}
		if o.Ungreedy {
			opt.Flags |= tre.Ungreedy
		}

		//

		if o.NotBOL {
			eflags |= tre.NotBOL
		}

		if o.NotEOL {
			eflags |= tre.NotEOL
		}

		if o.BacktrackingMatcher {
			eflags |= tre.BacktrackingMatcher
		}

		//

		findAll = o.FindAll
		no_sub = o.NoSub
	}

	var re *tre.Regexp

	re, err = tre.CompileOptions(pattern, opt)

	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	var output_matches [][]int = make([][]int, 0)

	text_runes := []rune(text)

	for start := 0; start <= len(text_runes); {

		var matches []tre.Match

		matches, err = re.Exec(text_runes[start:], re.NumSubexp()+1, eflags)

		if err == tre.ErrNoMatch {
			break
		}

		if err != nil {
			fmt.Fprintln(os.Stderr, err)

			return
		}

		var one_match []int

		for _, m := range matches {
			if m.So < 0 || m.Eo < 0 {
				one_match = append(one_match, -1, -1)
			} else {
				one_match = append(one_match, m.So+start, m.Eo+start)
			}
		}

		output_matches = append(output_matches, one_match)

		if !findAll || no_sub {
			break
		}

		if one_match[0] == one_match[1] { // empty match
			start++
		} else {
			start = one_match[1] // end of match
		}
	}

	//

	var output outputStruct

	output.Matches = output_matches

	output_json, err := json.Marshal(output)

	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	fmt.Printf("%s\n", output_json)
}
