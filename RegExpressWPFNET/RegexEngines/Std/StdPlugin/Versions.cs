namespace StdPlugin;

static class Versions
{
    public static string MSVC { get; } = "14.51.36244"; // see `_VC_CRT_MAJOR_VERSION` etc. in 'crtversion.h'
    public static string GCC { get; } = "16.1.0";
    public static string SRELL { get; } = "2026.08";
    public static string SRELL_LINEAR { get; } = "2026.04";
}
