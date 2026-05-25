// https://github.com/zig-utils/zig-regex

const std = @import("std");
const Io = std.Io;
const json = std.json;

const Regex0 = @import("zig-regex/src/regex.zig");
const RegexCommon = @import("zig-regex/src/common.zig");
const Regex = Regex0.Regex;

// NOTE. 'defer' is not always used; memory will be freed automatically at the end.

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

    //try reader.appendRemainingUnlimited(allocator, &input_al);

    try reader.appendRemaining(allocator, &input_al, std.Io.Limit.unlimited);

    const input_string = input_al.items;
    //\\{ "pattern": "(?<first>\\d)(\\d*)(?<last>QQQ)?", "text": "a1b23c456", "flags": "" }

    //std.debug.print("input_string: '{s}'\n", .{input_string});

    const INPUT_TYPE = struct { pattern: []u8, text: []u8, flags: []u8 };

    const input_parsed_object = try json.parseFromSlice(INPUT_TYPE, allocator, input_string, .{});
    //defer input_parsed_object.deinit();

    const input_object = input_parsed_object.value;

    const input_flags = input_object.flags;

    const flags: RegexCommon.CompileFlags =
        .{
            .case_insensitive = std.mem.indexOf(u8, input_flags, "i") != null,
            .multiline = std.mem.indexOf(u8, input_flags, "m") != null,
            .dot_all = std.mem.indexOf(u8, input_flags, "s") != null,
            .extended = std.mem.indexOf(u8, input_flags, "x") != null,
            .unicode = std.mem.indexOf(u8, input_flags, "U") != null,
        };

    var regex = try Regex.compileWithFlags(allocator, input_object.pattern, flags);
    //defer regex.deinit();

    const GROUP = struct { value: ?[]const u8 };
    const MATCH = struct { start: usize, length: usize, groups: ?[]GROUP };
    const OUTPUT = struct { names: ?[]?[]const u8, matches: ?[]MATCH };

    var output_object: OUTPUT = .{ .names = null, .matches = null };

    const named_captures = regex.named_captures;
    var names: std.ArrayList(?[]const u8) = .empty;
    try names.appendNTimes(allocator, null, regex.capture_count);

    var iter = named_captures.iterator();
    while (iter.next()) |entry| {
        const n = entry.key_ptr.*;
        const i = entry.value_ptr.*;
        names.items[i - 1] = n;
    }

    output_object.names = names.items;

    const matches = try regex.findAll(allocator, input_object.text);

    var matches_arr: std.ArrayList(MATCH) = .empty;

    for (matches) |*m| {
        var groups_arr: std.ArrayList(GROUP) = .empty;

        for (m.captures) |c| { // (no default group)

            try groups_arr.append(allocator, GROUP{ .value = c });
        }

        try matches_arr.append(allocator, MATCH{ .start = m.start, .length = m.end - m.start, .groups = groups_arr.items });
    }

    output_object.matches = matches_arr.items;

    const json_options: std.json.Stringify.Options = .{ .whitespace = .indent_2 };

    const output_json = try std.fmt.allocPrint(allocator, "{f}\n", .{std.json.fmt(output_object, json_options)});

    try stdout.writeStreamingAll(init.io, output_json);

    // defer {
    //     for (matches) |*m| {
    //         var mut_m = m;
    //         mut_m.deinit(allocator);
    //     }
    //     allocator.free(matches);
    // }
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
