#include "mac_monitor.hpp"
#include <CoreServices/CoreServices.h>

struct file_monitor::mac_monitor::detail
{
  static void change_event(ConstFSEventStreamRef stream,
                           void* context_info,
                           size_t event_count,
                           void* event_data,
                           FSEventStreamEventFlags const event_flags[],
                           FSEventStreamEventId const event_ids[])
  {
    auto that = static_cast<mac_monitor*>(context_info);
    auto paths = reinterpret_cast<char**>(event_data);

    for (int i = 0; i < event_count; i++)
    {
      that->path_changed(paths[i]);
    }
  }

  static void stop_source_signalled(void*)
  {
    CFRunLoopStop(CFRunLoopGetCurrent());
  }
};

void file_monitor::mac_monitor::start(path_t const& where)
{
  m_keep_running = true;
  this->m_base_path = where;

  // Create a stream for the filesystem events
  CFStringRef path_string =
    CFStringCreateWithCString(nullptr, where.string().c_str(), kCFStringEncodingUTF8);
  CFArrayRef paths = CFArrayCreate(nullptr, (const void**)&path_string, 1, nullptr);
  CFAbsoluteTime latency = 1.0; // latency in seconds
  FSEventStreamContext context{};
  context.info = this;
  auto Stream = FSEventStreamCreate(nullptr,
                                    &detail::change_event,
                                    &context,
                                    paths,
                                    kFSEventStreamEventIdSinceNow,
                                    latency,
                                    kFSEventStreamCreateFlagNone);

  // Create a source for the one-shot stop signal
  CFRunLoopSourceContext stop_source_context = {};
  stop_source_context.perform = &detail::stop_source_signalled;
  m_stop_source = CFRunLoopSourceCreate(nullptr, 0, &stop_source_context);

  // Start the event handling thread
  this->m_event_thread = std::thread([this, Stream]() { run(Stream); });
}

void file_monitor::mac_monitor::run(FSEventStreamRef stream)
{
  this->m_run_loop = CFRunLoopGetCurrent();
  this->hash_files_in(m_base_path);
  FSEventStreamScheduleWithRunLoop(stream, m_run_loop, kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);

  CFRunLoopAddSource(m_run_loop, m_stop_source, kCFRunLoopDefaultMode);
  CFRunLoopRun();
}

file_monitor::mac_monitor::hashcode_t file_monitor::mac_monitor::hash_file(path_t const& filepath)
{
  boost::iostreams::mapped_file_source mapped_file(filepath.string());

  return adler32(reinterpret_cast<std::uint8_t const*>(mapped_file.data()), mapped_file.size());
}

void file_monitor::mac_monitor::hash_files_in(path_t const& root)
{
  using Iterator = boost::filesystem::recursive_directory_iterator;

  for (auto& each : boost::make_iterator_range(Iterator(root), {}))
  {
    if (!m_keep_running)
      return;

    if (!is_regular_file(each.status()))
      continue;

    auto Path = relative_path(each.path());

    try
    {
      auto Hash = hash_file(each.path());
      m_file_hash_for[Path] = Hash;
    }
    catch (std::exception const& Error)
    {
      // Files that cannot be hashed are just ignored
    }
  }
}

file_monitor::path_t file_monitor::mac_monitor::relative_path(path_t const& file)
{
  path_t result;
  auto prefix = std::distance(m_base_path.begin(), m_base_path.end());

  for (auto each : boost::make_iterator_range(boost::next(file.begin(), prefix), file.end()))
    result /= each;

  return result;
}

void file_monitor::mac_monitor::path_changed(path_t const& where)
{
  // We can use the non-recursive iterator if the event flags say the change was not in
  // subdirs, but that is an optimization only
  using iterator_t = boost::filesystem::recursive_directory_iterator;

  for (auto& each : boost::make_iterator_range(iterator_t(where), {}))
  {
    if (!is_regular_file(each.status()))
      continue;

    auto path = relative_path(each.path());

    auto new_hash = hash_file(each.path());
    auto const& old_hash = m_file_hash_for[path];
    if (new_hash != old_hash)
    {
      m_file_hash_for[path] = new_hash;
      std::lock_guard<std::mutex> lock(m_file_list_mutex);
      m_changed.push_back(path);
    }
  }
}

void file_monitor::mac_monitor::stop()
{
  m_keep_running = false;
  if (m_event_thread.joinable())
  {
    CFRunLoopSourceSignal(m_stop_source);
    CFRunLoopWakeUp(m_run_loop);

    m_event_thread.join();
  }
}

file_monitor::mac_monitor::~mac_monitor()
{
  stop();
}

file_monitor::mac_monitor::mac_monitor()
{
}

void file_monitor::mac_monitor::poll(change_event_t const& consumer)
{
  std::lock_guard<std::mutex> guard(m_file_list_mutex);

  if (!m_changed.empty())
  {
    consumer(m_base_path, m_changed);
    m_changed.clear();
  }
}

std::uint32_t file_monitor::mac_monitor::adler32(std::uint8_t const* data, std::size_t size)
{
  std::uint32_t ADLER_PRIME = 65521;
  std::uint32_t a = 1;
  std::uint32_t b = 0;

  for (std::size_t i = 0; i < size; ++i)
  {
    a = (a + data[i]) % ADLER_PRIME;
    b = (b + a) % ADLER_PRIME;
  }

  return (b << 16) | a;
}
