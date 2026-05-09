function(_reflect_write_libcxx_gen dir)
  file(MAKE_DIRECTORY "${dir}")
  file(WRITE "${dir}/__config_site"
[[#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE
#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_ABI_FORCE_ITANIUM 1
#define _LIBCPP_ABI_FORCE_MICROSOFT 0
#define _LIBCPP_HAS_THREADS 1
#define _LIBCPP_HAS_MONOTONIC_CLOCK 1
#define _LIBCPP_HAS_TERMINAL 1
#define _LIBCPP_HAS_MUSL_LIBC 0
#define _LIBCPP_HAS_THREAD_API_PTHREAD 1
#define _LIBCPP_HAS_THREAD_API_EXTERNAL 0
#define _LIBCPP_HAS_THREAD_API_WIN32 0
#define _LIBCPP_HAS_FILESYSTEM 1
#define _LIBCPP_HAS_RANDOM_DEVICE 1
#define _LIBCPP_HAS_LOCALIZATION 1
#define _LIBCPP_HAS_UNICODE 1
#define _LIBCPP_HAS_WIDE_CHARACTERS 1
#define _LIBCPP_HAS_TIME_ZONE_DATABASE 0
#define _LIBCPP_HAS_VENDOR_AVAILABILITY_ANNOTATIONS 0
#define _LIBCPP_INSTRUMENTED_WITH_ASAN 0
#define _LIBCPP_HARDENING_MODE_DEFAULT 2
#define _LIBCPP_PSTL_BACKEND_STD_THREAD 1
#endif
]])
  file(WRITE "${dir}/__assertion_handler"
[[#ifndef _LIBCPP___ASSERTION_HANDLER
#define _LIBCPP___ASSERTION_HANDLER
#include <__config>
#include <__verbose_abort>
#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif
#if _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_DEBUG
#  define _LIBCPP_ASSERTION_HANDLER(m) _LIBCPP_VERBOSE_ABORT("%s", m)
#else
#  if __has_builtin(__builtin_verbose_trap)
#    define _LIBCPP_ASSERTION_HANDLER(m) __builtin_verbose_trap("libc++", m)
#  else
#    define _LIBCPP_ASSERTION_HANDLER(m) ((void)m, __builtin_trap())
#  endif
#endif
#endif
]])
endfunction()
