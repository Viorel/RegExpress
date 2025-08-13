import sys
import re
import json
import regex

input_json = sys.stdin.read()

#print( input_json, file = sys.stderr )

input_obj = json.loads(input_json)

module = input_obj['module']
pattern = input_obj['pattern']
text = input_obj['text']
flags_obj = input_obj['flags']
timeout = input_obj['timeout']

flags = 0
if flags_obj['ASCII']       : flags |= re.ASCII
if flags_obj['DOTALL']      : flags |= re.DOTALL
if flags_obj['IGNORECASE']  : flags |= re.IGNORECASE
if flags_obj['LOCALE']      : flags |= re.LOCALE
if flags_obj['MULTILINE']   : flags |= re.MULTILINE
if flags_obj['VERBOSE']     : flags |= re.VERBOSE

if module == 'regex':
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
    regex_obj = None

    if module == 'regex':
        regex_obj = regex.compile( pattern, flags)
    else:
        regex_obj = re.compile( pattern, flags)

    #print( f'# {regex_obj.groups}')
    #print( f'# {regex_obj.groupindex}')

    for key, value in regex_obj.groupindex.items():
        print( f'N {value} <{key}>')

    matches = None

    if module == 'regex':
        matches = regex_obj.finditer( text, overlapped = flags_obj['overlapped'], partial = flags_obj['partial'], timeout = timeout )
    else:
        matches = regex_obj.finditer( text )

    for match in matches :
        print( f'M {match.start()}, {match.end()}')
        for g in range(0, regex_obj.groups + 1):
            print( f'G {match.start(g)}, {match.end(g)}' )

except:
    ex_type, ex, tb = sys.exc_info()

    print( ex, file = sys.stderr )
