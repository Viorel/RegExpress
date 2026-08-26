const std = @import("std");
const Io = std.Io;
const json = std.json;

const EziGexWorker = @import("EziGexWorker");
const gex = @import("ezi_gex");

const OPTIONS_TYPE = struct {
    case_insensitive: bool,
    multiline: bool,
    dot_matches_newline: bool,
    unicode: bool,
    unicode_word_boundary_in_dfa: bool,
    prefilter: bool,
    case_fold: []u8,
    max_repetition: u32,
    byte_engine: []u8,
    simd: []u8,
};

const INPUT_TYPE = struct {
    pattern: []u8,
    text: []u8,
    //options: OPTIONS_TYPE = .{},
};

const OUTPUT = struct {
    names: ?[]?[]const u8,
    matches: ?[][][]i32,
};

pub fn main1(init: std.process.Init) !void {
    @setRuntimeSafety(true);

    const allocator = init.arena.allocator();
    const stdin = Io.File.stdin();
    const stdout = Io.File.stdout();

    var stdin_buffer: [512]u8 = undefined;
    var stdin_reader_wrapper = stdin.readerStreaming(init.io, &stdin_buffer);
    const reader: *std.Io.Reader = &stdin_reader_wrapper.interface;

    var input_al: std.ArrayList(u8) = .empty;

    try reader.appendRemaining(allocator, &input_al, std.Io.Limit.unlimited);

    const input_string = input_al.items;

    const input_parsed_object = try json.parseFromSlice(INPUT_TYPE, allocator, input_string, .{});
    //defer input_parsed_object.deinit();

    const input_object = input_parsed_object.value;
    //const input_options = input_object.options;

    const pattern = input_object.pattern;
    const text = input_object.text;

    const options: gex.Options = .{ .case_fold = .full, .unicode = true };
    //options.case_insensitive = input_options.case_insensitive;

    var diag: gex.Diagnostic = .{};

    var re = try gex.compileRuntime(allocator, pattern, &diag, options);
    //defer re.deinit();

    var sc = try @TypeOf(re).Scratch.init(allocator, &re.program);
    //defer sc.deinit(allocator);

    var output_object: OUTPUT = .{ .names = null, .matches = null };

    var names: std.ArrayList(?[]const u8) = .empty;
    for (1..re.captureCount() + 1) |i| { // exclude default group
        try names.append(allocator, re.groupName(i));
    }
    output_object.names = names.items;

    var matches_arr: std.ArrayList([][]i32) = .empty;

    const slots = try allocator.alloc(?usize, re.slotCount());
    //defer allocator.free(slots);

    var it = re.capturesAll(&sc, slots, text); // iterate every match
    while (it.next()) |hit| {
        //const m = hit.match();

        var groups_arr: std.ArrayList([]i32) = .empty;

        for (0..re.captureCount() + 1) |i| { // exclude default group
            const g = hit.group(i);
            var group_arr: std.ArrayList(i32) = .empty;
            if (g) |c| {
                try group_arr.append(allocator, @intCast(c.start));
                try group_arr.append(allocator, @intCast(c.end));
            } else {
                try group_arr.append(allocator, -1);
                try group_arr.append(allocator, -1);
            }

            try groups_arr.append(allocator, group_arr.items);
        }

        try matches_arr.append(allocator, groups_arr.items);
    }

    output_object.matches = matches_arr.items;

    const json_options: std.json.Stringify.Options = .{ .whitespace = .minified, .escape_unicode = true };
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
        const error_text = try std.fmt.allocPrint(allocator, "{s}\n", .{@errorName(err)});
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

    const error_text = std.fmt.allocPrint(allocator, "{s}\n", .{msg}) catch "Catastrophic failure";
    stderr.writeStreamingAll(init.io, error_text) catch {};

    std.process.exit(1);
}
