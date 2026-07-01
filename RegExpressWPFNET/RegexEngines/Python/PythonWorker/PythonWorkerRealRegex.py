import sys
import json
import real as re

input_json = sys.stdin.read()

#print( input_json, file = sys.stderr )

input_obj = json.loads(input_json)

pattern     = input_obj['pattern']
text        = input_obj['text']
flags_obj   = input_obj['flags']

flags = 0
#if flags_obj['ASCII']       : flags |= re.ASCII
if flags_obj['DOTALL']      : flags |= re.DOTALL
if flags_obj['IGNORECASE']  : flags |= re.IGNORECASE
#if flags_obj['LOCALE']      : flags |= re.LOCALE
if flags_obj['MULTILINE']   : flags |= re.MULTILINE
if flags_obj['VERBOSE']     : flags |= re.VERBOSE

try:
    regex_obj = re.compile( pattern, flags)

    #print( f'# {regex_obj.groups}')
    #print( f'# {regex_obj.groupindex}')

    for key, value in regex_obj.groupindex.items():
        print( f'N {value} <{key}>')

    matches = regex_obj.finditer( text )

    for match in matches :
        print( f'M {match.start()}, {match.end()}')
        for g in range(0, regex_obj.groups + 1) :
            print( f'g {match.start(g)}, {match.end(g)}' )

except:
    ex_type, ex, tb = sys.exc_info()

    print( ex, file = sys.stderr )
