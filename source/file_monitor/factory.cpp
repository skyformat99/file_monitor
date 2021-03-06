#include "factory.hpp"
#include "debouncer.hpp"

#ifdef FILE_MONITOR_PLATFORM_MACOS
#include "mac_monitor.hpp"
#endif

#ifdef FILE_MONITOR_PLATFORM_WIN32
#include "win_monitor.hpp"
#endif

#ifdef FILE_MONITOR_PLATFORM_LINUX
#include "linux_monitor.hpp"
#endif

std::shared_ptr<file_monitor::monitor> file_monitor::make_platform_monitor()
{
#if defined(FILE_MONITOR_PLATFORM_MACOS)
  return std::make_shared<mac_monitor>();
#elif defined(FILE_MONITOR_PLATFORM_WIN32)
  return std::make_shared<win_monitor>();
#elif defined(FILE_MONITOR_PLATFORM_LINUX)
  return std::make_shared<linux_monitor>();
#else
#error No supported platform defined
#endif
}

std::shared_ptr<file_monitor::monitor> file_monitor::make_monitor()
{
  return std::make_shared<file_monitor::debouncer>(make_platform_monitor());
}
