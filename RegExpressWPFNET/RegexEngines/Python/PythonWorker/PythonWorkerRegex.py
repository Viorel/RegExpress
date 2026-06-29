import sys
import json
import regex

input_json = sys.stdin.read()

#print( input_json, file = sys.stderr )

input_obj = json.loads(input_json)

pattern     = input_obj['pattern']
text        = input_obj['text']
flags_obj   = input_obj['flags']
timeout     = input_obj['timeout']

flags = 0
if flags_obj['ASCII']       : flags |= regex.ASCII
if flags_obj['DOTALL']      : flags |= regex.DOTALL
if flags_obj['IGNORECASE']  : flags |= regex.IGNORECASE
if flags_obj['LOCALE']      : flags |= regex.LOCALE
if flags_obj['MULTILINE']   : flags |= regex.MULTILINE
if flags_obj['VERBOSE']     : flags |= regex.VERBOSE

if flags_obj['BESTMATCH']       : flags |= regex.BESTMATCH
if flags_obj['ENHANCEMATCH']    : flags |= regex.ENHANCEMATCH
if flags_obj['FULLCASE']        : flags |= regex.FULLCASE
if flags_obj['POSIX']           : flags |= regex.POSIX
if flags_obj['REVERSE']         : flags |= regex.REVERSE
if flags_obj['UNICODE']         : flags |= regex.UNICODE
if flags_obj['WORD']            : flags |= regex.WORD
if flags_obj['VERSION0']        : flags |= regex.VERSION0
if flags_obj['VERSION1']        : flags |= regex.VERSION1 

try:
    regex_obj = regex.compile( pattern, flags)

    #print( f'# {regex_obj.groups}')
    #print( f'# {regex_obj.groupindex}')

    for key, value in regex_obj.groupindex.items():
        print( f'N {value} <{key}>')

    matches = regex_obj.finditer( text, overlapped = flags_obj['overlapped'], partial = flags_obj['partial'], timeout = timeout )

    for match in matches :
        print( f'M {match.start()}, {match.end()}')
        for g in range(0, regex_obj.groups + 1) :
            print( f'g {match.start(g)}, {match.end(g)}' )
            if g != 0 :
                for c in match.spans(g) :
                    print( f'c {c[0]}, {c[1]}')

except:
    ex_type, ex, tb = sys.exc_info()

    print( ex, file = sys.stderr )
