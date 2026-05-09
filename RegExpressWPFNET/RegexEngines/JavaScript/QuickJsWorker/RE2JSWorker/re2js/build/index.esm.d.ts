/**
 * A stateful iterator that interprets a regex {@code RE2JS} on a specific input.
 *
 * Conceptually, a Matcher consists of four parts:
 * <ol>
 * <li>A compiled regular expression {@code RE2JS}, set at construction and fixed for the lifetime
 * of the matcher.</li>
 *
 * <li>The remainder of the input string, set at construction or {@link #reset()} and advanced by
 * each match operation such as {@link #find}, {@link #matches} or {@link #lookingAt}.</li>
 *
 * <li>The current match information, accessible via {@link #start}, {@link #end}, and
 * {@link #group}, and updated by each match operation.</li>
 *
 * <li>The append position, used and advanced by {@link #appendReplacement} and {@link #appendTail}
 * if performing a search and replace from the input to an external {@code StringBuffer}.
 *
 * </ol>
 *
 *
 * @author rsc@google.com (Russ Cox)
 */
export class Matcher {
    /**
     * Quotes '\' and '$' in {@code s}, so that the returned string could be used in
     * {@link #appendReplacement} as a literal replacement of {@code s}.
     *
     * @param {string} str the string to be quoted
     * @param {boolean} [javaMode=false] whether the replacement will be used in javaMode
     * @returns {string} the quoted string
     */
    static quoteReplacement(str: string, javaMode?: boolean): string;
    /**
     *
     * @param {RE2JS} pattern
     * @param {string|number[]|Uint8Array} input
     */
    constructor(pattern: RE2JS, input: string | number[] | Uint8Array);
    /**
     * The pattern being matched.
     * @type {RE2JS}
     */
    patternInput: RE2JS;
    /** @type {number} */
    patternGroupCount: number;
    /** @type {number[]} */
    groups: number[];
    /** @type {Record<string, number>} */
    namedGroups: Record<string, number>;
    /** @type {number} */
    numberOfInstructions: number;
    /**
     * Returns the {@code RE2JS} associated with this {@code Matcher}.
     * @returns {RE2JS}
     */
    pattern(): RE2JS;
    /**
     * Resets the {@code Matcher}, rewinding input and discarding any match information.
     *
     * @returns {Matcher} the {@code Matcher} itself, for chained method calls
     */
    reset(): Matcher;
    /** @type {number} */
    matcherInputLength: number | undefined;
    /** @type {number} */
    appendPos: number | undefined;
    hasMatch: boolean | undefined;
    hasGroups: boolean | undefined;
    anchorFlag: number | undefined;
    /**
     * Resets the {@code Matcher} and changes the input.
     * @param {MatcherInputBase} input
     * @returns {Matcher} the {@code Matcher} itself, for chained method calls
     */
    resetMatcherInput(input: MatcherInputBase): Matcher;
    matcherInput: MatcherInputBase | undefined;
    /**
     * Returns the start of the named group of the most recent match, or -1 if the group was not
     * matched.
     * @param {string|number} [group=0]
     * @returns {number}
     */
    start(group?: string | number): number;
    /**
     * Returns the end of the named group of the most recent match, or -1 if the group was not
     * matched.
     * @param {string|number} [group=0]
     * @returns {number}
     */
    end(group?: string | number): number;
    /**
     * Returns the program size of this pattern.
     *
     * <p>
     * Similar to the C++ implementation, the program size is a very approximate measure of a regexp's
     * "cost". Larger numbers are more expensive than smaller numbers.
     * </p>
     *
     * @returns {number} the program size of this pattern
     */
    programSize(): number;
    /**
     * Returns the named group of the most recent match, or {@code null} if the group was not matched.
     * @param {string|number} [group=0]
     * @returns {string|null}
     */
    group(group?: string | number): string | null;
    /**
     * Returns a dictionary map of all named capturing groups and their matched values.
     * If a group was not matched, its value will be `null`.
     * @returns {Record<string, string|null>}
     */
    getNamedGroups(): Record<string, string | null>;
    /**
     * Returns the number of subgroups in this pattern.
     *
     * @returns {number} the number of subgroups; the overall match (group 0) does not count
     */
    groupCount(): number;
    /**
     * Helper: finds subgroup information if needed for group.
     * @param {number} group
     * @private
     */
    private loadGroup;
    /**
     * Matches the entire input against the pattern (anchored start and end). If there is a match,
     * {@code matches} sets the match state to describe it.
     *
     * @returns {boolean} true if the entire input matches the pattern
     */
    matches(): boolean;
    /**
     * Matches the beginning of input against the pattern (anchored start). If there is a match,
     * {@code lookingAt} sets the match state to describe it.
     *
     * @returns {boolean} true if the beginning of the input matches the pattern
     */
    lookingAt(): boolean;
    /**
     * Matches the input against the pattern (unanchored), starting at a specified position. If there
     * is a match, {@code find} sets the match state to describe it.
     *
     * @param {number|null} [start=null] the input position where the search begins
     * @returns {boolean} if it finds a match
     * @throws IndexOutOfBoundsException if start is not a valid input position
     */
    find(start?: number | null): boolean;
    /**
     * Helper: does match starting at start, with RE2 anchor flag.
     * @param {number} startByte
     * @param {number} anchor
     * @returns {boolean}
     * @private
     */
    private genMatch;
    /**
     * Helper: return substring for [start, end).
     * @param {number} start
     * @param {number} end
     * @returns {string}
     */
    substring(start: number, end: number): string;
    /**
     * Helper for Pattern: return input length.
     * @returns {number}
     */
    inputLength(): number;
    /**
     * Appends to result two strings: the text from the append position up to the beginning of the
     * most recent match, and then the replacement with submatch groups substituted for references of
     * the form {@code $n}, where {@code n} is the group number in decimal. It advances the append
     * position to where the most recent match ended.
     *
     * To embed a literal {@code $}, use \$ (actually {@code "\\$"} with string escapes). The escape
     * is only necessary when {@code $} is followed by a digit, but it is always allowed. Only
     * {@code $} and {@code \} need escaping, but any character can be escaped.
     *
     * The group number {@code n} in {@code $n} is always at least one digit and expands to use more
     * digits as long as the resulting number is a valid group number for this pattern. To cut it off
     * earlier, escape the first digit that should not be used.
     *
     * @param {string} replacement the replacement string
     * @param {boolean} [javaMode=false] activate java mode (different behaviour for capture groups and special characters)
     * @returns {string}
     * @throws IllegalStateException if there was no most recent match
     * @throws IndexOutOfBoundsException if replacement refers to an invalid group
     * @private
     */
    private appendReplacement;
    /**
     * @param {string} replacement - the replacement string
     * @returns {string}
     * @private
     */
    private appendReplacementInternalJava;
    /**
     * @param {string} replacement - the replacement string
     * @returns {string}
     * @private
     */
    private appendReplacementInternalJs;
    /**
     * Return the substring of the input from the append position to the end of the
     * input.
     * @returns {string}
     */
    appendTail(): string;
    /**
     * Returns the input with all matches replaced by {@code replacement}, interpreted as for
     * {@code appendReplacement}.
     *
     * @param {string} replacement - the replacement string
     * @param {boolean} [javaMode=false] - activate java mode (different behaviour for capture groups and special characters)
     * @returns {string} the input string with the matches replaced
     * @throws IndexOutOfBoundsException if replacement refers to an invalid group and javaMode is true
     */
    replaceAll(replacement: string, javaMode?: boolean): string;
    /**
     * Returns the input with the first match replaced by {@code replacement}, interpreted as for
     * {@code appendReplacement}.
     *
     * @param {string} replacement - the replacement string
     * @param {boolean} [javaMode=false] - activate java mode (different behaviour for capture groups and special characters)
     * @returns {string} the input string with the first match replaced
     * @throws IndexOutOfBoundsException if replacement refers to an invalid group and javaMode is true
     */
    replaceFirst(replacement: string, javaMode?: boolean): string;
    /**
     * Helper: replaceAll/replaceFirst hybrid.
     * @param {string} replacement - the replacement string
     * @param {boolean} [all=true] - replace all matches
     * @param {boolean} [javaMode=false] - activate java mode (different behaviour for capture groups and special characters)
     * @returns {string}
     * @private
     */
    private replace;
}
/**
 * A compiled representation of an RE2 regular expression
 *
 * The matching functions take {@code String} arguments instead of the more general Java
 * {@code CharSequence} since the latter doesn't provide UTF-16 decoding.
 *
 *
 * @author rsc@google.com (Russ Cox)
 * @class
 */
export class RE2JS {
    /**
     * Flag: case insensitive matching.
     */
    static CASE_INSENSITIVE: number;
    /**
     * Flag: dot ({@code .}) matches all characters, including newline.
     */
    static DOTALL: number;
    /**
     * Flag: multiline matching: {@code ^} and {@code $} match at beginning and end of line, not just
     * beginning and end of input.
     */
    static MULTILINE: number;
    /**
     * Flag: Unicode groups (e.g. {@code \p\ Greek\} ) will be syntax errors.
     */
    static DISABLE_UNICODE_GROUPS: number;
    /**
     * Flag: matches longest possible string.
     */
    static LONGEST_MATCH: number;
    /**
     * Flag: enable linear-time captureless lookbehinds.
     */
    static LOOKBEHINDS: number;
    /**
     * Returns a literal pattern string for the specified string.
     *
     * This method produces a string that can be used to create a <code>RE2JS</code> that would
     * match the string <code>s</code> as if it were a literal pattern.
     *
     * Metacharacters or escape sequences in the input sequence will be given no special meaning.
     *
     * @param {string} str The string to be literalized
     * @returns {string} A literal string replacement
     */
    static quote(str: string): string;
    /**
     * Quotes '\' and '$' in {@code str}, so that the returned string could be used in
     * replacement methods as a literal replacement of {@code str}.
     *
     * This is a convenience delegation to {@link Matcher.quoteReplacement}.
     *
     * @param {string} str the string to be quoted
     * @param {boolean} [javaMode=false] whether the replacement will be used in javaMode
     * @returns {string} the quoted string
     */
    static quoteReplacement(str: string, javaMode?: boolean): string;
    /**
     * Translates a given regular expression string to ensure compatibility with RE2JS.
     *
     * This function preprocesses the input regex string by applying necessary transformations,
     * such as escaping special characters (e.g., `/`), converting named capture groups to
     * RE2JS-compatible syntax, and handling Unicode sequences properly. It ensures that the
     * resulting regex is safe and properly formatted before compilation.
     *
     * @param {string|RegExp} expr - The regular expression string to be translated.
     * @returns {string} - The transformed regular expression string, ready for compilation.
     */
    static translateRegExp(expr: string | RegExp): string;
    /**
     * Helper: create new RE2JS with given regex and flags. Flregex is the regex with flags applied.
     * @param {string} regex
     * @param {number} [flags=0]
     * @returns {RE2JS}
     */
    static compile(regex: string, flags?: number): RE2JS;
    /**
     * Matches a string against a regular expression.
     *
     * @param {string} regex the regular expression
     * @param {string|number[]|Uint8Array} input the input
     * @returns {boolean} true if the regular expression matches the entire input
     * @throws RE2JSSyntaxException if the regular expression is malformed
     */
    static matches(regex: string, input: string | number[] | Uint8Array): boolean;
    /**
     * This is visible for testing.
     * @private
     */
    private static initTest;
    /**
     *
     * @param {string} pattern
     * @param {number} flags
     */
    constructor(pattern: string, flags: number);
    patternInput: string;
    flagsInput: number;
    /**
     * Releases memory used by internal caches associated with this pattern. Does not change the
     * observable behaviour. Useful for tests that detect memory leaks via allocation tracking.
     */
    reset(): void;
    /**
     * Returns the flags used in the constructor.
     * @returns {number}
     */
    flags(): number;
    /**
     * Returns the pattern used in the constructor.
     * @returns {string}
     */
    pattern(): string;
    re2(): any;
    /**
     * Matches a string against a regular expression.
     *
     * @param {string|number[]|Uint8Array} input the input
     * @returns {boolean} true if the regular expression matches the entire input
     */
    matches(input: string | number[] | Uint8Array): boolean;
    /**
     * Creates a new {@code Matcher} matching the pattern against the input.
     *
     * @param {string|number[]|Uint8Array} input the input string
     * @returns {Matcher}
     */
    matcher(input: string | number[] | Uint8Array): Matcher;
    /**
     * Tests whether the regular expression matches any part of the input string.
     * Performance Note: This method is highly optimized. Because it only returns
     * a boolean and does not extract capture groups, it bypasses the `Matcher` overhead
     * and guarantees execution on the high-speed DFA engine whenever possible.
     *
     * @param {string|number[]|Uint8Array} input - The input string or UTF-8 byte array to test against.
     * @returns {boolean} `true` if the pattern is found anywhere in the input, `false` otherwise.
     */
    test(input: string | number[] | Uint8Array): boolean;
    /**
     * Tests whether the regular expression matches the ENTIRE input string.
     * * **Performance Note:** This operates identically to `.matches()`, but is significantly
     * faster because it does not request capture group data. By requesting 0 capture groups,
     * it securely routes execution through the DFA fast-path.
     *
     * @param {string|number[]|Uint8Array} input - The input string or UTF-8 byte array to test against.
     * @returns {boolean} `true` if the exact input string fully matches the pattern, `false` otherwise.
     */
    testExact(input: string | number[] | Uint8Array): boolean;
    /**
     * Splits input around instances of the regular expression. It returns an array giving the strings
     * that occur before, between, and after instances of the regular expression.
     *
     * If {@code limit <= 0}, there is no limit on the size of the returned array. If
     * {@code limit == 0}, empty strings that would occur at the end of the array are omitted. If
     * {@code limit > 0}, at most limit strings are returned. The final string contains the remainder
     * of the input, possibly including additional matches of the pattern.
     *
     * @param {string} input the input string to be split
     * @param {number} [limit=0] the limit
     * @returns {string[]} the split strings
     */
    split(input: string, limit?: number): string[];
    /**
     *
     * @returns {string}
     */
    toString(): string;
    /**
     * Returns the program size of this pattern.
     *
     * <p>
     * Similar to the C++ implementation, the program size is a very approximate measure of a regexp's
     * "cost". Larger numbers are more expensive than smaller numbers.
     * </p>
     *
     * @returns {number} the program size of this pattern
     */
    programSize(): number;
    /**
     * Returns the number of capturing groups in this matcher's pattern. Group zero denotes the entire
     * pattern and is excluded from this count.
     *
     * @returns {number} the number of capturing groups in this pattern
     */
    groupCount(): number;
    /**
     * Return a map of the capturing groups in this matcher's pattern, where key is the name and value
     * is the index of the group in the pattern.
     * @returns {Record<string, number>}
     */
    namedGroups(): Record<string, number>;
    /**
     *
     * @param {*} other
     * @returns {boolean}
     */
    equals(other: any): boolean;
}
/**
 * An exception thrown by the compiler
 */
export class RE2JSCompileException extends RE2JSException {
}
export class RE2JSException extends Error {
    /** @param {string} message */
    constructor(message: string);
}
/**
 * An exception thrown by flags
 */
export class RE2JSFlagsException extends RE2JSException {
}
/**
 * An exception thrown by using groups
 */
export class RE2JSGroupException extends RE2JSException {
}
/**
 * An exception thrown for internal engine errors, such as corrupted bytecodes.
 */
export class RE2JSInternalException extends RE2JSException {
}
/**
 * An exception thrown by the parser if the pattern was invalid.
 */
export class RE2JSSyntaxException extends RE2JSException {
    /**
     * @param {string} error
     * @param {string|null} [input=null]
     */
    constructor(error: string, input?: string | null);
    /** @type {string} */
    error: string;
    /** @type {string|null} */
    input: string | null;
    /**
     * Retrieves the description of the error.
     * @returns {string}
     */
    getDescription(): string;
    /**
     * Retrieves the erroneous regular-expression pattern.
     * @returns {string|null}
     */
    getPattern(): string | null;
}
export class RE2Set {
    /** @type {number} */
    static UNANCHORED: number;
    /** @type {number} */
    static ANCHOR_START: number;
    /** @type {number} */
    static ANCHOR_BOTH: number;
    /**
     * Constructs a new RE2Set with the specified anchor mode and flags.
     * @param {number} [anchor=RE2Set.UNANCHORED] - The anchoring mode (e.g., RE2Set.UNANCHORED).
     * @param {number} [flags=0] - The public flags to apply to all patterns in the set.
     */
    constructor(anchor?: number, flags?: number);
    anchor: number;
    jsFlags: number;
    re2Flags: number;
    regexps: any[];
    prog: Prog | null;
    dfa: DFA | null;
    dummyRe2: {
        prog: Prog;
        cond: number;
        prefix: string;
        prefixRune: number;
        longest: boolean;
    } | null;
    /**
     * Adds a new regular expression pattern to the set.
     * Patterns cannot be added after the set has been compiled.
     * @param {string} pattern - The regular expression pattern to add.
     * @returns {number} The integer index assigned to the added pattern.
     * @throws {RE2JSCompileException} If patterns are added after compilation.
     */
    add(pattern: string): number;
    /**
     * Compiles the added patterns into a single state machine.
     * This is automatically called on the first match if not called explicitly.
     * @returns {void}
     */
    compile(): void;
    /**
     * Matches the input against the compiled set of regular expressions.
     * @param {string|number[]|Uint8Array} input - The input string or UTF-8 byte array to match against.
     * @returns {number[]} An array of indices representing the patterns that successfully matched the input.
     */
    match(input: string | number[] | Uint8Array): number[];
}
export function re(stringsOrFlags: any, ...values: any[]): RE2JS | ((strings: any, ...tagValues: any[]) => RE2JS);
/**
 * Abstract the representations of input text supplied to Matcher.
 */
declare class MatcherInputBase {
    static Encoding: any;
    getEncoding(): void;
    /**
     *
     * @returns {boolean}
     */
    isUTF8Encoding(): boolean;
    /**
     *
     * @returns {boolean}
     */
    isUTF16Encoding(): boolean;
}
/**
 * A Prog is a compiled regular expression program.
 */
declare class Prog {
    inst: any[];
    start: number;
    numCap: number;
    lbStarts: any[];
    numLb: number;
    getInst(pc: any): any;
    numInst(): number;
    addInst(op: any): void;
    skipNop(pc: any): any;
    prefix(): (string | boolean)[];
    startCond(): number;
    patch(l: any, val: any): void;
    append(l1: any, l2: any): any;
    /**
     *
     * @returns {string}
     */
    toString(): string;
}
declare class DFA {
    static MAX_CACHE_CLEARS: number;
    constructor(prog: any);
    prog: any;
    stateCache: Map<any, any>;
    stateCount: number;
    startState: any;
    stateLimit: number;
    cacheClears: number;
    failed: boolean;
    clock: number;
    computeClosure(pcs: any): {
        pcs: Int32Array<ArrayBuffer>;
        isMatch: boolean;
        matchIDs: any[];
    } | null;
    getState(pcs: any): any;
    evictCache(): void;
    step(state: any, charCode: any, anchor: any): any;
    match(input: any, pos: any, anchor: any): boolean | null;
    matchSet(input: any, pos: any, anchor: any): any[] | null;
}
export {};
//# sourceMappingURL=index.esm.d.ts.map