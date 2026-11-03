const std = @import("std");
const Io = std.Io;
const json = std.json;

//const ZRegexWorker = @import("ZRegexWorker");
const zregex = @import("zregex");

const OPTIONS_TYPE = struct {
    case_insensitive: bool = false,
    multiline: bool = false,
    dot_all: bool = false,
};

const INPUT_TYPE = struct {
    pattern: []u8,
    text: []u8,
    options: OPTIONS_TYPE = .{},
};

const OUTPUT_TYPE = struct {
    names: ?[]?[]const u8,
    matches: ?[][]i32,
};

pub fn main1(init: std.process.Init) !void {
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

    const pattern = input_object.pattern;
    const text = input_object.text;
    const options = input_object.options;

    const flags: zregex.Flags =
        .{
            .case_insensitive = options.case_insensitive,
            .multiline = options.multiline,
            .dot_all = options.dot_all,
        };

    const re = try zregex.Regex.compileWithFlags(allocator, pattern, flags);

    var iterator = re.iterator(allocator, text);

    var matches_arr: std.ArrayList([]i32) = .empty;

    while (try iterator.next()) |match| {
        var groups_arr: std.ArrayList(i32) = .empty;

        for (match.groups) |group| {
            if (group == null) {
                try groups_arr.append(allocator, -1);
                try groups_arr.append(allocator, -1);
            } else {
                try groups_arr.append(allocator, @intCast(group.?.start));
                try groups_arr.append(allocator, @intCast(group.?.end));
            }
        }

        try matches_arr.append(allocator, groups_arr.items);
    }

    //

    var names_arr: std.ArrayList(?[]const u8) = .empty;

    for (re.names) |named_group| {

        // Note. 'names_arr.resize' does not initialize elements to null
        if (named_group.index > names_arr.items.len) {
            try names_arr.appendNTimes(allocator, null, named_group.index - names_arr.items.len);
        }

        names_arr.items[named_group.index - 1] = named_group.name;
    }

    const output_object: OUTPUT_TYPE = .{ .names = names_arr.items, .matches = matches_arr.items };
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
