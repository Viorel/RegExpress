const std = @import("std");
const Io = std.Io;
const json = std.json;

const pzre = @import("pzre");
//const compile = pzre.compile;
//const Match = pzre.Match;

const ZigPzreWorker = @import("ZigPzreWorker");

// NOTE. 'defer' is not always used; memory will be freed automatically at the end.

const FLAG_TYPE = struct {
    is_debug: bool = false,
};

const INPUT_TYPE = struct {
    pattern: []u8,
    text: []u8,
    flags: FLAG_TYPE = .{},
};

const MATCH = struct {
    start: usize,
    length: usize,
};

const OUTPUT = struct {
    matches: ?[]MATCH,
};

pub fn main1(init: std.process.Init) !void {
    @setRuntimeSafety(true);

    const allocator = init.arena.allocator();
    const stdin = Io.File.stdin();
    const stdout = Io.File.stdout();
    //const stderr = Io.File.stderr();

    var stdin_buffer: [512]u8 = undefined;
    var stdin_reader_wrapper = stdin.readerStreaming(init.io, &stdin_buffer);
    const reader: *std.Io.Reader = &stdin_reader_wrapper.interface;

    var input_al: std.ArrayList(u8) = .empty;

    try reader.appendRemaining(allocator, &input_al, std.Io.Limit.unlimited);

    const input_string = input_al.items;

    //std.debug.print("input_string: '{s}'\n", .{input_string});

    const input_parsed_object = try json.parseFromSlice(INPUT_TYPE, allocator, input_string, .{});
    //defer input_parsed_object.deinit();

    const input_object = input_parsed_object.value;
    const input_flags = input_object.flags;
    const is_debug = input_flags.is_debug;

    // TODO: flags

    const arch = pzre.Arch{ .minimal_nfa = .{ .context = .{ .dynamic = .u8 } } };
    const config = pzre.compile.Config{};

    var re = try pzre.regex.compile(arch, config, allocator, input_object.pattern);
    defer re.deinit(allocator);

    var ctx = try re.initContext(allocator);
    defer ctx.deinit(allocator);

    var matches_arr: std.ArrayList(MATCH) = .empty;

    var previous_start: ?usize = null;
    var iter = re.matchIter(&ctx, input_object.text);

    while (iter.next()) |m| {
        if (m.loc.start == previous_start) @panic("Infinite loop. No advance.");

        previous_start = m.loc.start;

        try matches_arr.append(allocator, MATCH{ .start = m.loc.start, .length = m.loc.len() });
    }

    const output_object: OUTPUT = .{ .matches = matches_arr.items };

    const json_options: std.json.Stringify.Options = .{ .whitespace = if (is_debug) .indent_2 else .minified };

    const output_json = try std.fmt.allocPrint(allocator, "{f}\n", .{std.json.fmt(output_object, json_options)});

    try stdout.writeStreamingAll(init.io, output_json);
}

var init_arg: ?std.process.Init = null;

pub fn main(init: std.process.Init) !void {
    init_arg = init;
    @setRuntimeSafety(true);

    const allocator = init.arena.allocator();
    const stderr = Io.File.stderr();

    main1(init) catch |err| {
        const error_text = try std.fmt.allocPrint(allocator, "Error: {s}\n", .{@errorName(err)});
        try stderr.writeStreamingAll(init.io, error_text);
        std.process.exit(1);
    };

    std.process.exit(0);
}

pub const panic = std.debug.FullPanic(myPanic);

fn myPanic(msg: []const u8, first_trace_addr: ?usize) noreturn {
    _ = first_trace_addr;

    const init: std.process.Init = init_arg.?; //' orelse std.process.exit(1); //...............

    const allocator = init.arena.allocator();
    //const stdin = Io.File.stdin();
    //const stdout = Io.File.stdout();
    const stderr = Io.File.stderr();

    const error_text = std.fmt.allocPrint(allocator, "Panic: {s}\n", .{msg}) catch "Catastrophic failure";
    stderr.writeStreamingAll(init.io, error_text) catch {};

    std.process.exit(1);
}
