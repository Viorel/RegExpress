const std = @import("std");
const Io = std.Io;
const json = std.json;

//const Zoptia0regexWorker = @import("Zoptia0regexWorker");
const zoptia0regex = @import("zoptia0regex");

const OPTIONS_TYPE = struct {
    is_debug: bool = false,
    posix: bool = false,
    longest: bool = false,
};

const INPUT_TYPE = struct {
    pattern: []u8,
    text: []u8,
    options: OPTIONS_TYPE = .{},
};

const OUTPUT = struct {
    names: []const []const u8,
    matches: ?[][]i64,
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

    // TODO: POSIX

    var re: zoptia0regex.Regexp = undefined;

    if (input_object.options.posix) {
        re = try zoptia0regex.compilePOSIX(allocator, input_object.pattern);
    } else {
        re = try zoptia0regex.compile(allocator, input_object.pattern);
    }

    if (input_object.options.longest) re.setLongest();

    //try re.matches(allocator, input_object.text, 0);

    const matches = try re.findAllSubmatchIndex(allocator, input_object.text, -1);

    const output: OUTPUT = .{ .names = re.subexp_names, .matches = matches };

    const json_options: std.json.Stringify.Options = .{ .whitespace = if (input_object.options.is_debug) .indent_2 else .minified };

    const output_json = try std.fmt.allocPrint(allocator, "{f}\n", .{std.json.fmt(output, json_options)});

    try stdout.writeStreamingAll(init.io, output_json);

    //const t1 = try std.fmt.allocPrint(allocator, "{f}\n", .{std.json.fmt(input_object, json_options)});
    //try stdout.writeStreamingAll(init.io, t1);
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
