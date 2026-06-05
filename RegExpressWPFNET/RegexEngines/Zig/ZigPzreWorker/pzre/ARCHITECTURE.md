# Architecture
back to [readme](README.md)
It is recommended to first familiarize one-self with the [reference](REFERENCE.md)

## API objects
### Regex
### AnyRegex

## Compilation pipeline
### Safety
#### Allocation upper bound
```zig
var gpa = CountingAllocator.init(child_allocator, config.limits.gpa_upper_bound);
return compileWithModel(config, .dynamic, gpa.allocator(), pattern) catch |err| switch (err) {
  error.OutOfMemory => {
    // Differentiate between reaching the resource cap, over a system out of memory error
    return if (gpa.cap_reached) error.AllocationUpperbound else error.OutOfMemory;
  },
  else => return err,
};
```

## Contexts

## Memory model polymorphism
