#define UMDF_USING_NTSTATUS // https://stackoverflow.com/questions/60903656/how-do-i-deal-with-both-winnt-h-and-ntstatus-h-both-in-the-wdk

#include <algorithm>
#include <any>  
#include <array>    
#include <assert.h> 
#include <atomic>   
//#include <bcrypt.h> 
#include <bit>
#include <bitset>   
#include <cassert>  
#include <cctype>   
#include <cerrno>   
#include <cfloat>  // for DBL_DIG and FLT_DIG   
#include <chrono>   
#include <cinttypes>    
#include <ciso646>  
#include <climits>  
#include <cmath>    
#include <compare>  
#include <condition_variable>  // NOLINT(build/c++11)   
#include <csignal>  
#include <cstdarg>  
#include <cstddef>  
#include <cstdint>  
#include <cstdio>   
#include <cstdlib>  
#include <cstring>  
#include <ctime>    
#include <cwchar>   
//#include <dbghelp.h>    
#include <deque>    
#include <emmintrin.h>  
#include <errno.h>  
#include <exception>    
#include <fcntl.h>  
#include <filesystem>  // NOLINT    
#include <forward_list> 
#include <fstream>  
#include <functional>   
#include <immintrin.h>  
#include <initializer_list> 
#include <intrin.h> 
#include <io.h> 
#include <iomanip>  
#include <ios>  
#include <iosfwd>   
#include <iostream> 
#include <istream>  
#include <iterator> 
#include <limits.h> 
#include <limits>   
#include <list> 
#include <map>  
#include <memory>   
#include <mutex>  // NOLINT(build/c++11)    
#include <new>  
#include <numeric>  
#include <optional> 
#include <ostream>  
#include <queue>    
#include <random>   
#include <ranges>  // NOLINT(build/c++20)   
#include <ratio>  // NOLINT(build/c++11)    
#include <roapi.h>  
#include <sanitizer/asan_interface.h>   
#include <sanitizer/common_interface_defs.h>
#include <sanitizer/hwasan_interface.h>
#include <sanitizer/lsan_interface.h>   
#include <sanitizer/msan_interface.h>   
#include <sanitizer/tsan_interface.h>   
#include <sdkddkver.h>  
#include <set>  
#include <signal.h> 
#include <span>  // NOLINT(build/c++20) 
#include <sstream>  
#include <stdbool.h>    
#include <stddef.h>
#include <stdexcept>    
#include <stdint.h>
#include <stdio.h>  
#include <stdlib.h> 
#include <streambuf>    
#include <string.h> 
#include <string_view>  
#include <string>   
#include <sys/stat.h>   
#include <sys/types.h>  
#include <system_error>  // NOLINT(build/c++11) 
#include <tchar.h>  
#include <thread>  // NOLINT(build/c++11)   
#include <time.h>   
#include <tmmintrin.h>  
#include <tuple>    
#include <type_traits>
#include <typeinfo> 
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>  
#include <vector>   
#include <version>  
#include <wchar.h>  
#include <winapifamily.h>   
#include <windows.globalization.h>  
#include <windows.h>    
#include <winsock2.h>  // for timeval   
#include <winstring.h>  
#include <xmmintrin.h>  
