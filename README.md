# test_resource

## Overview
The repository provides the enhancement of [Bloomberg's P1160 C++ proposal](https://github.com/bloomberg/p1160) of *std::pmr::test_resource type*.


## Supported Enhancements
* the header only implementation
* [memory alignment](#memory-alignment)
* [test_resource_reporter](#type-test_resource_reporter)


### Memory Alignment
The *test_resource* type supports the memory allocation/deallocation for objects of type with the alignment up to 4096 Bytes.


### Type *test_resource_reporter*
The original Bloomberg's implementation bound memory allocation/deallocation actions with the logging actions
and the log information is output to the console only.
The modification introduces a new type *test_resource_reporter*.
The type plays a role of a base 'interface' type of further reporter implementations.
The design of the logging feature is based on the [Non-Virtual-Interface](https://en.wikibooks.org/wiki/More_C++_Idioms/Non-Virtual_Interface) idiom, utilized by std::pmr::memory_resource too.
```c++
namespace stdx::pmr {
class test_resource_reporter
{
public:
  virtual ~test_resource_reporter() noexcept;

  void report_allocation(const test_resource& tr);

  void report_deallocation(const test_resource& tr);

  void report_release(const test_resource& tr);

  void report_invalid_memory_block(
    const test_resource& tr,
    std::size_t deallocatedBytes,
    std::size_t deallocatedAlignment,
    int underrunBy,
    int overrunBy);

  void report_print(const test_resource& tr);

  void report_log_msg(const char* format, ...);

protected:
  [[nodiscard]]
  static const detail::test_resource_list* test_resource_list(const test_resource& tr) noexcept;

private:
  virtual void do_report_allocation(const test_resource& tr) = 0;

  virtual void do_report_deallocation(const test_resource& tr) = 0;

  virtual void do_report_release(const test_resource& tr) = 0;

  virtual void do_report_invalid_memory_block(
    const test_resource& tr,
    std::size_t deallocatedBytes,
    std::size_t deallocatedAlignment,
    int underrunBy,
    int overrunBy) = 0;

  virtual void do_report_print(const test_resource& tr) = 0;

  virtual void do_report_log_msg(const char* format, va_list args) = 0;
};
}
```
The interface opens a way to implement a new kind of reporter (XML, JSON, etc. ),
which may be passed as a constructor parameter to the *test_reporter* instance.


#### Implemented Reporter Types
The modified implementation of 'test_resource' type provides three kinds of reporters:

<b>1. *null_test_resource_reporter*</b> - no log information is produced
```c++
[[nodiscard]]
stdx::pmr::test_resource_reporter* null_test_resource_reporter() noexcept;
```

<b>2. *console_test_resource_reporter*</b> - the log information is routed to stdout.
```c++
[[nodiscard]]
stdx::pmr::test_resource_reporter* console_test_resource_reporter() noexcept;
```

<b>3. *file_test_resource_reporter*</b> - the log information is routed to the file.
```c++
namespace stdx::pmr {
class file_test_resource_reporter
{
public:
  file_test_resource_reporter() noexcept;

  explicit file_test_resource_reporter(
    const std::filesystem::path& filename,
    std::ios_base::openmode mode = std::ios_base::out);

  void open(const std::filesystem::path& filename,
    std::ios_base::openmode mode = std::ios_base::out);

  void close();

  [[nodiscard]]
  bool good() const;

protected:
  void do_report_allocation(const test_resource& tr) override;

  void do_report_deallocation(const test_resource& tr) override;

  void do_report_release(const test_resource& tr) override;

  void do_report_invalid_memory_block(
    const test_resource& tr,
    std::size_t deallocatedBytes,
    std::size_t deallocatedAlignment,
    int underrunBy,
    int overrunBy) override;

  void do_report_print(const test_resource& tr) override;

  void do_report_log_msg(const char* format, va_list args) override;

private:
    std::ofstream m_fstream;
};
}
// the previous code is a presentation of a functional interface;
// in the reality, the file_test_resource_reporter is a type alias

using file_test_resource_reporter = detail::file_reporter<detail::stream_test_resource_reporter>;

// the detail::file_reporter is a kind of adapter template type which
// takes a StreamReporter as template type parameter [CRTP pattern](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).
// The StreamReporter type needs to define the test_resource_reporter's abstract interface.
namespace stdx::pmr::detail {
template<typename StreamReporter,
  typename = std::enable_if_t<
    std::is_base_of_v<test_resource_reporter, StreamReporter>>
>
class file_reporter : public StreamReporter
{
...
};
}
```

#### Default Reporter Type
The *console_test_resource_reporter* type is taken as a default kind of reporter.

The current default test resource reporter can be detected via:
```c++
[[nodiscard]]
test_resource_reporter* get_default_test_resource_reporter() noexcept;
```

The current default test resource reporter can be modified via:
```c++
test_resource_reporter* set_default_test_resource_reporter(test_resource_reporter* reporter = nullptr) noexcept
```

## Not Supported Features
* the registration of more reporters per a 'test_resource' instance is not provided,
* the chaining of reporters is not supported,
* the console_test_resource_reporter and file_test_resource_reporter do not support wide-characters.
