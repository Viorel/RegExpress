package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"time"

	"unicode/utf8"

	onigmo "github.com/go-regexp/engine"
)

type inputStruct struct {
	Pattern string
	Text    string

	FindAll   bool
	TimeoutNs *int64
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

	re, err := onigmo.Compile(pattern)

	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	if input.TimeoutNs != nil {
		re = re.WithTimeout(time.Duration(*input.TimeoutNs))
	}

	names := re.SubexpNames() // []string
	//fmt.Printf( "names: %q\n", names)

	var all_matches [][]int

	// TODO: replace the loop with 'FindAllStringSubmatchIndex' when such function is available

	for start := 0; start <= len(text); {

		matches, err := re.FindStringSubmatchIndexErr(text[start:]) // []int, example: [0,1,0,1,-1,-1] (first group succeeded, second group failed)

		if err != nil {
			fmt.Fprintln(os.Stderr, err)

			return
		}

		if len(matches) < 2 { // ('len(nil)' works too and is zero)
			break
		}

		matches_adjusted := make([]int, len(matches))

		for i, v := range matches {
			if v < 0 {
				matches_adjusted[i] = v
			} else {
				matches_adjusted[i] = v + start
			}
		}

		all_matches = append(all_matches, matches_adjusted)

		if !input.FindAll {
			break
		}

		last_start := matches_adjusted[0]
		last_end_exclusive := matches_adjusted[1]

		if last_start == last_end_exclusive { // empty match

			if last_end_exclusive == len(text) {
				break
			}

			for i := 1; i <= utf8.UTFMax; i++ {
				if utf8.Valid([]byte(text[last_end_exclusive : last_end_exclusive+i])) {
					start = last_end_exclusive + i
					break
				}
			}
		} else {
			start = last_end_exclusive
		}
	}

	var output outputStruct

	output.Names = names
	output.Matches = all_matches

	output_json, err := json.Marshal(output)

	if err != nil {
		fmt.Fprintln(os.Stderr, err)

		return
	}

	fmt.Printf("%s\n", output_json)
}
