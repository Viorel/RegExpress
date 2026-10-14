import sys
import json
import real as re


def outputMatch(match):
    print( f'M {match.start()}, {match.end()}')
    for g in range(0, regex_obj.groups + 1) :
        print( f'g {match.start(g)}, {match.end(g)}' )


input_json = sys.stdin.read()

#print( input_json, file = sys.stderr )

input_obj = json.loads(input_json)

pattern     = input_obj['pattern']
text        = input_obj['text']
flags_obj   = input_obj['flags']

flags = re.NOFLAG
if flags_obj['IGNORECASE']  : flags |= re.IGNORECASE
if flags_obj['MULTILINE']   : flags |= re.MULTILINE
if flags_obj['DOTALL']      : flags |= re.DOTALL
if flags_obj['UNICODE']     : flags |= re.UNICODE # no-op
if flags_obj['VERBOSE']     : flags |= re.VERBOSE
if flags_obj['ASCII']       : flags |= re.ASCII

fallback = False
if flags_obj['fallback']    : fallback = True

fullmatch = ('fullmatch' in flags_obj) and flags_obj['fullmatch']

limits = re.Limits(
    program_size = flags_obj['program_size'] if 'program_size' in flags_obj else None,
    repeat_count = flags_obj['repeat_count'] if 'repeat_count' in flags_obj else None,
    group_count = flags_obj['group_count'] if 'group_count' in flags_obj else None,
    nesting_depth = flags_obj['nesting_depth'] if 'nesting_depth' in flags_obj else None,
    lookaround_length = flags_obj['lookaround_length'] if 'lookaround_length' in flags_obj else None,
    dfa_memory = flags_obj['dfa_memory'] if 'dfa_memory' in flags_obj else None
    )

try:
    regex_obj = re.compile( pattern, flags, fallback, limits=limits)

    #print( f'# {regex_obj.groups}')
    #print( f'# {regex_obj.groupindex}')

    for key, value in regex_obj.groupindex.items():
        print( f'N {value} <{key}>')

    if fullmatch:
        match = regex_obj.fullmatch( text)

        if match != None:
            outputMatch( match)
    else:
        matches = regex_obj.finditer( text )

        for match in matches :
            outputMatch( match)

except:
    ex_type, ex, tb = sys.exc_info()

    print( ex, file = sys.stderr )
