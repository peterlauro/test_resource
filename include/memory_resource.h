#ifndef STDX_MEMORYRESOURCE_H
#define STDX_MEMORYRESOURCE_H

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <mutex>
#include <string_view>
#include <type_traits>

namespace stdx::pmr
{
  class test_resource;

  namespace detail
  {
    struct test_resource_list;
  }

  class test_resource_reporter
  {
  public:
    virtual ~test_resource_reporter() noexcept = default;

    void report_allocation(const test_resource& tr)
    {
      do_report_allocation(tr);
    }

    void report_deallocation(const test_resource& tr)
    {
      do_report_deallocation(tr);
    }

    void report_release(const test_resource& tr)
    {
      do_report_release(tr);
    }

    void report_invalid_memory_block(
      const test_resource& tr,
      std::size_t deallocatedBytes,
      std::size_t deallocatedAlignment,
      int underrunBy,
      int overrunBy)
    {
      do_report_invalid_memory_block(
        tr,
        deallocatedBytes,
        deallocatedAlignment,
        underrunBy,
        overrunBy);
    }

    void report_print(const test_resource& tr)
    {
      do_report_print(tr);
    }

    void report_log_msg(const char* format, ...)
    {
      va_list args;
      va_start(args, format);
      do_report_log_msg(format, args);
      va_end(args);
    }

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

  // Proposal P1160R1 - maybe part of C++23
  // Add Test Polymorphic Memory Resource to the Standard Library
  // https://github.com/bloomberg/p1160
  // https://www.youtube.com/watch?v=48oAZqlyx_g&feature=youtu.be
  // http://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1160r1.pdf
  // https://www.youtube.com/watch?v=v3dz-AKOVL8
  namespace detail
  {
    /**
     * \brief Checks if passed integer value is a power of two
     * \tparam IntegerType the type of value
     * \param val value to check
     * \return true if passed integer value is a power of two, otherwise false
     */
    template<typename IntegerType>
    constexpr bool is_power_of_two(IntegerType val) noexcept
    {
      static_assert(std::is_integral<IntegerType>::value,
        "An argument for is_power_of_two should be integral type");
      return val && (0 == (val & (val - 1)));
    }

    /**
     * \brief Checks whether the address of object is aligned by given alignment
     * \param ptr the address of object
     * \param alignment alignment shall be a power of two
     * \return true - if the value of the first argument is aligned on the boundary specified by alignment
     *         false - otherwise
     */
    inline bool is_aligned(const void* ptr, std::size_t alignment) noexcept
    {
      return is_power_of_two(alignment) ? (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1U)) == 0U : false;
    }

    // magic number identifying memory allocated by this resource
    // dead beef - "EF BE AD DE" on little endian
    inline constexpr std::uint32_t allocated_memory_pattern{ 0xDEADBEEFU };

    // magic number written over other magic number upon deallocation
    // dead food - "0D F0 AD DE" on little endian
    inline constexpr std::uint32_t deallocated_memory_pattern{ 0xDEADF00DU };

    // byte (1010 0101) used to scribble deallocated memory
    inline constexpr std::byte scribbled_memory_byte{ 0xA5U };

    // byte (1011 0001) used to write over newly-allocated memory and padding
    inline constexpr std::byte padded_memory_byte{ 0xB1U };

    // Holds pointers to the next and preceding allocated
    // memory block in the allocated memory block list.
    struct block
    {
      long long m_index;  // index of this allocation
      block*    m_next;   // next 'block' pointer
      block*    m_prev;   // previous 'block' pointer
    };

    //  --------------------------------------------------------------------
    //  | HEADER + PADDING + [additional padding] | USER SEGMENT | PADDING |
    //  --------------------------------------------------------------------

    inline constexpr std::size_t max_natural_alignment = alignof(std::max_align_t);
    // size of the padding before and after the user segment
    inline constexpr std::size_t padding_size = max_natural_alignment;

    struct alignas(max_natural_alignment) padding
    {
      std::byte m_padding[padding_size];
    };

    /**
     * \brief defines the data preceding the user segment
     */
    struct header
    {
      std::uint32_t m_magic_number;  // allocated/deallocated/other identifier
      std::size_t   m_bytes;         // number of available bytes in this block
      std::size_t   m_alignment;     // the allocation alignment
      long long     m_index;         // index of this memory allocation
      block*        m_address;       // address of block in linked list
      void*         m_pmr;           // address of current PMR
      padding       m_padding;       // padding -- guaranteed to extend to the
                                     // end of the struct
    };

    struct aligned_header_base
    {
      header m_object;
    };

    // to suppress MSVC warning C4324:
    // structure was padded due to alignment specifier
    template<std::size_t Align>
    struct aligned_header_with_additional_padding_base : aligned_header_base
    {
      std::byte _[Align - (sizeof(aligned_header_base) % Align)];
    };

    constexpr std::size_t checked_alignment(std::size_t alignment) noexcept
    {
      return std::max(alignment, max_natural_alignment);
    }

    // Maximally-aligned raw buffer big enough for a header.
    template<std::size_t Align>
    struct aligned_header;

    template<std::size_t Align>
    using aligned_type = std::conditional_t<
      Align <= max_natural_alignment || sizeof(aligned_header_base) % Align == 0U,
      aligned_header_base,
      aligned_header_with_additional_padding_base<checked_alignment(Align)>>;

    template<>
    struct alignas(checked_alignment(1U)) aligned_header<1U> : aligned_type<1U>
    {
    };

    template<>
    struct alignas(checked_alignment(2U)) aligned_header<2U> : aligned_type<2U>
    {
    };

    template<>
    struct alignas(checked_alignment(4U)) aligned_header<4U> : aligned_type<4U>
    {
    };

    template<>
    struct alignas(checked_alignment(8U)) aligned_header<8U> : aligned_type<8U>
    {
    };

    template<>
    struct alignas(checked_alignment(16U)) aligned_header<16U> : aligned_type<16U>
    {
    };

    template<>
    struct alignas(checked_alignment(32U)) aligned_header<32U> : aligned_type<32U>
    {
    };

    template<>
    struct alignas(checked_alignment(64U)) aligned_header<64U> : aligned_type<64U>
    {
    };

    template<>
    struct alignas(checked_alignment(128U)) aligned_header<128U> : aligned_type<128U>
    {
    };

    template<>
    struct alignas(checked_alignment(256U)) aligned_header<256U> : aligned_type<256U>
    {
    };

    template<>
    struct alignas(checked_alignment(512U)) aligned_header<512U> : aligned_type<512U>
    {
    };

    template<>
    struct alignas(checked_alignment(1024U)) aligned_header<1024U> : aligned_type<1024U>
    {
    };

    template<>
    struct alignas(checked_alignment(2048U)) aligned_header<2048U> : aligned_type<2048U>
    {
    };

    template<>
    struct alignas(checked_alignment(4096U)) aligned_header<4096U> : aligned_type<4096U>
    {
    };

    template<std::size_t Align>
    inline constexpr auto aligned_header_size_v = sizeof(aligned_header<Align>);

    template<std::size_t Align>
    inline constexpr auto aligned_header_align_v = alignof(aligned_header<Align>);

    inline header* get_header(void *p, std::size_t alignment)
    {
      std::size_t aligned_header_size = 0;
      switch (alignment)
      {
      case 1U:
        aligned_header_size = aligned_header_size_v<1U>;
        break;
      case 2U:
        aligned_header_size = aligned_header_size_v<2U>;
        break;
      case 4U:
        aligned_header_size = aligned_header_size_v<4U>;
        break;
      case 8U:
        aligned_header_size = aligned_header_size_v<8U>;
        break;
      case 16U:
        aligned_header_size = aligned_header_size_v<16U>;
        break;
      case 32U:
        aligned_header_size = aligned_header_size_v<32U>;
        break;
      case 64U:
        aligned_header_size = aligned_header_size_v<64U>;
        break;
      case 128U:
        aligned_header_size = aligned_header_size_v<128U>;
        break;
      case 256U:
        aligned_header_size = aligned_header_size_v<256U>;
        break;
      case 512U:
        aligned_header_size = aligned_header_size_v<512U>;
        break;
      case 1024U:
        aligned_header_size = aligned_header_size_v<1024U>;
        break;
      case 2048U:
        aligned_header_size = aligned_header_size_v<2048U>;
        break;
      case 4096U:
        aligned_header_size = aligned_header_size_v<4096U>;
        break;
      default:
        break;
      }

      return aligned_header_size ? reinterpret_cast<header*>(reinterpret_cast<std::intptr_t>(p) - aligned_header_size) : nullptr;
    }

    // intrusive list of memory blocks
    struct test_resource_list
    {
      block* m_head = nullptr;  // address of first block in list (or 'nullptr')
      block* m_tail = nullptr;  // address of last block in list (or 'nullptr')

      /**
       * \brief Removes the specified 'mblock' from the 'List of Blocks'.
       * \param mblock address of memory block to remove
       * \return the address of the removed memory block
       * \note The behavior is undefined unless 'List of Blocks' and 'mblock'
       *       are non-zero. Note that the tail pointer of 'List of Blocks'
       *       will be updated if the 'mblock' removed is the tail itself,
       *       and the head pointer of the 'List of Blocks' will be updated if
       *       the 'mblock' removed is the head itself.
       */
      block* remove_block(block* mblock)
      {
        if (mblock == m_tail)
        {
          m_tail = mblock->m_prev;
        }
        else
        {
          mblock->m_next->m_prev = mblock->m_prev;
        }

        if (mblock == m_head)
        {
          m_head = mblock->m_next;
        }
        else
        {
          mblock->m_prev->m_next = mblock->m_next;
        }

        return mblock;
      }

      [[nodiscard]]
      bool empty() const noexcept
      {
        return !(m_head);
      }

      /**
       * \brief Adds newly allocated memory block with the specified 'index' into the 'list of blocks'
       *        The specified 'resource' polymorphic memory resource is used to allocated memory block;
       * \param index the index of new memory block
       * \param resource the memory_resource used to allocate a memory block
       * \return the address of the allocated memory 'block'
       * \note the head pointer of the 'list' will be updated if the 'list of blocks' is initially empty
       */
      block* add_block(long long index, std::pmr::memory_resource* resource)
      {
        auto* mblock = static_cast<block*>(resource->allocate(sizeof(block), alignof(block)));

        if (mblock)
        {
          mblock->m_next = nullptr;
          mblock->m_index = index;

          if (!m_head)
          {
            //empty list
            m_head = mblock;
            m_tail = mblock;
            mblock->m_prev = nullptr;
          }
          else
          {
            m_tail->m_next = mblock;
            mblock->m_prev = m_tail;
            m_tail = mblock;
          }

          return mblock;
        }

        return mblock;
      }

      /**
       * \brief Erases all memory blocks from the list with the given resource
       * \param resource the resource to be erased
       */
      void clear(std::pmr::memory_resource* resource)
      {
        block* mblock = m_head;
        while (mblock)
        {
          block* block_to_free = mblock;
          mblock = mblock->m_next;
          resource->deallocate(block_to_free, sizeof(block), alignof(block));
        }
        m_head = nullptr;
        m_tail = nullptr;
      }
    };

    class local_memory
    {
      struct malloc_free_resource final : std::pmr::memory_resource
      {
        malloc_free_resource() noexcept = default;

        [[nodiscard]]
        std::pmr::memory_resource* upstream_resource() const noexcept
        {
          return nullptr;
        }

      private:
        [[nodiscard]]
        void* do_allocate(std::size_t bytes, [[maybe_unused]] std::size_t alignment) override
        {
#ifdef _MSC_VER
          // Windows-specific API:
          void* p = ::_aligned_malloc(bytes, alignment);
#else
          // standard C++17 API:
          void* p = std::aligned_alloc(alignment, bytes);
#endif

          if (!p)
          {
            throw std::bad_alloc();
          }
          return p;
        }

        void do_deallocate(void* p, [[maybe_unused]] std::size_t bytes, [[maybe_unused]] std::size_t alignment) override
        {
#ifdef _MSC_VER
          // Windows-specific API:
          ::_aligned_free(p);
#else
          // standard C++17 API:
          std::free(p);
#endif
        }

        [[nodiscard]]
        bool do_is_equal(const std::pmr::memory_resource& that) const noexcept override
        {
          //return nullptr != dynamic_cast<const malloc_free_resource*>(&that);
          //return typeid(malloc_free_resource&) == typeid(that);
          //there is only one instance of malloc_free_resource in the process
          return this == &that;
        }
      };

    public:
      ~local_memory() = delete;

      static std::pmr::memory_resource* resource() noexcept
      {
        // immortalize the instance of malloc_free_resource type
        // the instance 'r' is never destructed;
        // the memory space where r is placed is deallocated,
        // when main function is finished
        using type = malloc_free_resource;
        alignas(type) static std::uint8_t buffer[sizeof(type)];
        static type* r = new (buffer) type;
        return r;
      }
    };

    template<typename CharT>
    class report_formater
    {
    public:
      using char_type = CharT;
      using traits_type = std::char_traits<char_type>;
      using string_type = std::basic_string<char_type, traits_type>;

      /**
       * \brief Format in hex to 'output' string, a block of memory starting at the specified
       *        starting 'address' of the specified 'length' (in bytes). Each line of
       *        formatted output will have a maximum of 16 bytes per line, where each
       *        line starts with the address of that 16-byte chunk.
       * \param address start address of memory block
       * \param length the length of memory block in bytes
       * \return string representation of specified memory block
       */
      static string_type mem2str(void* address, std::size_t length)
      {
        static constexpr std::size_t line_size = 128U;
        static constexpr std::size_t buff_size = line_size - 1U;

        char_type line[line_size] = { '\0' };
        int line_index = 0;

        auto* addr = static_cast<std::byte*>(address);
        std::byte* endAddr = addr + length;

        string_type output;
        output.reserve(((length / 16U) + 1U) * line_size);

        for (int i = 0; addr < endAddr; ++i)
        {
          if (0 == i % 4)
          {
            if (line_index != 0)
            {
              output.append(line);
            }
            line_index = 0;
            line[line_index] = { '\0' };
            if (i != 0)
            {
              line_index += snprintf(&line[line_index], buff_size - line_index, "\n");
            }
            line_index += snprintf(&line[line_index], buff_size - line_index, "%p:      ", static_cast<void*>(addr));
          }
          else
          {
            line_index += snprintf(&line[line_index], buff_size - line_index, "  ");
          }

          for (int j = 0; j < 4 && addr < endAddr; ++j)
          {
            line_index += snprintf(&line[line_index], buff_size - line_index, "%02x ", static_cast<int>(*addr));
            ++addr;
          }
        }

        output.append(line);
        output.append("\n");
        return output;
      }

      static string_type addr2str(void* address)
      {
        static constexpr std::size_t hexdigit_count = sizeof(std::intptr_t) * 2U;
        char_type line[hexdigit_count + 1U] = { '\0' };

        snprintf(&line[0U], hexdigit_count, "%p", address);

        return line;
      }

      static string_type msg2str(const char* format, va_list args)
      {
        static constexpr std::size_t buffer_size = 4095U;
        char_type buffer[buffer_size + 1U] = { '\0' };
        return 0 <= vsnprintf(buffer, buffer_size, format, args) ? string_type(buffer) : string_type();
      }
    };

    class stream_test_resource_reporter
      : public test_resource_reporter
    {
    public:
      explicit stream_test_resource_reporter(std::ostream& os) noexcept
        : m_stream(os)
      {}

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
      using char_type = std::ostream::char_type;
      using formater_type = report_formater<char_type>;
      using string_type = formater_type::string_type;

      std::ostream& m_stream;
    };

    template<typename StreamReporter>
    [[nodiscard]]
    test_resource_reporter* _console_test_resource_reporter() noexcept
    {
      alignas(StreamReporter) static std::uint8_t buffer[sizeof(StreamReporter)];
      static auto* reporter = new (buffer) StreamReporter(std::cout);
      return reporter;
    }

    [[nodiscard]]
    inline std::atomic<test_resource_reporter*>& _default_test_resource_reporter() noexcept
    {
      using type = std::atomic<test_resource_reporter*>;
      alignas(type) static std::uint8_t buffer[sizeof(type)];
      static type* reporter = new (buffer) type(_console_test_resource_reporter<stream_test_resource_reporter>());
      return *reporter;
    }

    template<
      typename StreamReporter,
      typename = std::enable_if_t<
        std::is_base_of_v<test_resource_reporter, std::decay_t<StreamReporter>>>
    >
    class file_reporter : public StreamReporter
    {
    public:
      file_reporter() noexcept
        : stream_test_resource_reporter(m_fstream)
      {
      }

      explicit file_reporter(
        const std::filesystem::path& filename,
        std::ios_base::openmode mode = std::ios_base::out)
        : stream_test_resource_reporter(m_fstream)
        , m_fstream(filename, mode)
      {
      }

      void open(const std::filesystem::path& filename,
        std::ios_base::openmode mode = std::ios_base::out)
      {
        m_fstream.open(filename, mode);
      }

      void close()
      {
        m_fstream.close();
      }

      [[nodiscard]]
      bool good() const
      {
        return m_fstream.good();
      }

    protected:
      void do_report_allocation(const test_resource& tr) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_allocation(tr);
        }
      }

      void do_report_deallocation(const test_resource& tr) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_deallocation(tr);
        }
      }

      void do_report_release(const test_resource& tr) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_release(tr);
        }
      }

      void do_report_invalid_memory_block(
        const test_resource& tr,
        std::size_t deallocatedBytes,
        std::size_t deallocatedAlignment,
        int underrunBy,
        int overrunBy) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_invalid_memory_block(
            tr,
            deallocatedBytes,
            deallocatedAlignment,
            underrunBy,
            overrunBy
          );
        }
      }

      void do_report_print(const test_resource& tr) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_print(tr);
        }
      }

      void do_report_log_msg(const char* format, va_list args) override
      {
        if (validate())
        {
          stream_test_resource_reporter::do_report_log_msg(format, args);
        }
      }

    private:
      [[nodiscard]]
      bool validate() const
      {
        return m_fstream.is_open();
      }

      std::ofstream m_fstream;
    };
  }

  using file_test_resource_reporter = detail::file_reporter<detail::stream_test_resource_reporter>;

  template<typename StreamReporter = detail::stream_test_resource_reporter,
    typename = std::enable_if_t<
      std::is_base_of_v<test_resource_reporter, std::decay_t<StreamReporter>>>
  >
  [[nodiscard]]
  decltype(auto)
  console_test_resource_reporter() noexcept
  {
    return detail::_console_test_resource_reporter<StreamReporter>();
  }

  [[nodiscard]]
  inline test_resource_reporter*
  null_test_resource_reporter() noexcept
  {
    class type final : public test_resource_reporter
    {
    public:
      type() noexcept = default;

    private:
      void do_report_allocation(const test_resource&) override
      {
      }

      void do_report_deallocation(const test_resource&) override
      {
      }

      void do_report_release(const test_resource&) override
      {
      }

      void do_report_invalid_memory_block(
        const test_resource&,
        std::size_t,
        std::size_t,
        int,
        int) override
      {
      }

      void do_report_print(const test_resource&) override
      {
      }

      void do_report_log_msg(const char*, va_list) override
      {
      }
    };

    // immortalize -> no destructor called on top of reporter
    alignas(type) static std::uint8_t buffer[sizeof(type)];
    static type* reporter = new (buffer) type;
    return reporter;
  }

  [[nodiscard]]
  inline test_resource_reporter*
  get_default_test_resource_reporter() noexcept
  {
    return detail::_default_test_resource_reporter().load();
  }

  inline test_resource_reporter*
  set_default_test_resource_reporter(test_resource_reporter* reporter = nullptr) noexcept
  {
    if (!reporter)
    {
        reporter = console_test_resource_reporter();
    }
    return detail::_default_test_resource_reporter().exchange(reporter);
  }

  /**
   * \brief The test_resource_exception is thrown by the test_resource
   *        when its allocation limit is reached and there is an attempt
   *        to allocate further memory. It is basically a special form of
   *        std::bad_alloc that allows the exception tester algorithm
   *        to differentiate between real out‐of‐memory situations from
   *        the test‐induced limits.
   * \note  The exception inherits from std::bad_alloc so that when it is thrown,
   *        the same code path will be traveled like in case of a real allocation
   *        failure. In other words: to ensure that we test the code that would
   *        run in production, in case std::bad_alloc is thrown by a memory resource.
   */
  class test_resource_exception : public std::bad_alloc
  {
  public:
    test_resource_exception(test_resource* originating, std::size_t size, std::size_t alignment) noexcept
      : m_originating(originating)
      , m_size(size)
      , m_alignment(alignment)
    {
    }

    [[nodiscard]]
    const char* what() const noexcept override
    {
      return "stdx::pmr::test_resource_exception";
    }

    [[nodiscard]]
    test_resource* originating_resource() const noexcept
    {
      return m_originating;
    }

    [[nodiscard]]
    std::size_t size() const noexcept
    {
      return m_size;
    }

    [[nodiscard]]
    std::size_t alignment() const noexcept
    {
      return m_alignment;
    }

  private:
    test_resource* m_originating;
    std::size_t m_size;
    std::size_t m_alignment;
  };

  /**
   * \brief The test_resource is a thread‐safe, instrumented memory resource that
   *        implements the standard std::pmr::memory_resource abstract interface
   *        and can be used to track various aspects of memory allocated from it,
   *        in addition to automatically detecting a number of memory management
   *        violations that might otherwise go unnoticed.
   * \note Features:
   *      - a thread‐safe implementation of the polymorphic memory resource interface
   *      - the detection of memory leaks
   *      - the detection of double releasing of memory
   *      - detection of writing before or beyond the allocated memory area (boundary violation)
   *      - overwriting memory just before deallocating it to help detect use of deleted memory
   *      - tracking of memory use statistics (number of outstanding blocks and bytes)
   *        that are currently in use, the cumulative number of blocks (and bytes) that have
   *        been allocated, and the maximum number of blocks (and bytes) that have been in use
   *        at any one time
   *      - monitoring of memory use changes (via the test_resource_monitor type)
   *      - temporary replacement of the default memory resource using the default_resource_guard
   *      - testing (exception safety) behavior in case of memory allocation failure
   *        (when the resource throws) using the test_allocation_failure algorithm
   */
  class test_resource final : public std::pmr::memory_resource
  {
    friend class test_resource_reporter;

  public:
    //constructors/destructors

    test_resource()
      : test_resource("", false, detail::local_memory::resource(), get_default_test_resource_reporter())
    {}

    explicit test_resource(std::pmr::memory_resource* upstream)
      : test_resource("", false, upstream, get_default_test_resource_reporter())
    {}

    explicit test_resource(const char* name)
      : test_resource(std::string_view(name))
    {}

    explicit test_resource(std::string_view name)
      : test_resource(name, false, detail::local_memory::resource(), get_default_test_resource_reporter())
    {}

    explicit test_resource(bool verbose, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : test_resource("", verbose, detail::local_memory::resource(), reporter)
    {}

    test_resource(std::string_view name, std::pmr::memory_resource* upstream)
      : test_resource(name, false, upstream, get_default_test_resource_reporter())
    {}

    test_resource(const char* name, std::pmr::memory_resource* upstream)
      : test_resource(std::string_view(name), upstream)
    {}

    test_resource(bool verbose, std::pmr::memory_resource* upstream, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : test_resource("", verbose, upstream, reporter)
    {}

    test_resource(std::string_view name, bool verbose, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : test_resource(name, verbose, detail::local_memory::resource(), reporter)
    {}

    test_resource(const char* name, bool verbose, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : test_resource(std::string_view(name), verbose, reporter)
    {}

    test_resource(std::string_view name, bool verbose, std::pmr::memory_resource* upstream, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : m_name(name)
      , m_verboseFlag(verbose)
      , m_reporter(reporter)
      , m_upstream(upstream)
    {
      //allocate and initialize the empty list of memory blocks
      m_list = new (m_upstream->allocate(sizeof(detail::test_resource_list))) detail::test_resource_list{};
    }

    test_resource(const char* name, bool verbose, std::pmr::memory_resource* upstream, test_resource_reporter* reporter = get_default_test_resource_reporter())
      : test_resource(std::string_view(name), verbose, upstream, reporter)
    {}

    ~test_resource() noexcept override
    {
      release();
    }

    test_resource(const test_resource&) = delete;
    test_resource& operator=(const test_resource&) = delete;

    /**
     * \brief Sets the allocation limit to the supplied limit.
     * \param limit supplied allocation limit
     * \note Any negative value for limit means there is no allocation
     *       limit imposed by this test memory resource.
     */
    void set_allocation_limit(long long limit) noexcept
    {
      m_allocationLimit.store(limit, std::memory_order_relaxed);
    }

    /**
     * \brief Sets the no‐abort behavior.
     * \param is_no_abort new value of no‐abort flag
     * \note If flag is true, do not abort the program upon detecting errors.
     *       The default value of the setting is false.
     */
    void set_no_abort(bool is_no_abort) noexcept
    {
      m_noAbortFlag.store(is_no_abort, std::memory_order_relaxed);
    }

    /**
     * \brief Sets the quiet behavior.
     * \param is_quiet new value of quiet flag
     * \note  If flag is true, do not report detected errors and imply is_no_abort() ==
     *        true.  The default value of the setting is false.
     */
    void set_quiet(bool is_quiet) noexcept
    {
      m_quietFlag.store(is_quiet, std::memory_order_relaxed);
    }

    /**
     * \brief Sets the verbose behavior.
     * \param is_verbose new value of verbose flag
     * \note If flag is true, report all allocations and deallocations to the standard output.
     *       The default value of the setting is false or what is specified in the constructor.
     */
    void set_verbose(bool is_verbose) noexcept
    {
      m_verboseFlag.store(is_verbose, std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of allocation requests permitted before throwing
     *        test_resource_exception or a negative value if this test memory resource
     *        does not impose a limit on the number of allocations
     * \return number of allowed allocation requests
     * \note This value will decrement with every call to do_allocate.
     */
    [[nodiscard]]
    long long allocation_limit() const noexcept
    {
      return m_allocationLimit.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the current no‐abort flag
     * \return the current no‐abort flag
     */
    [[nodiscard]]
    bool is_no_abort() const noexcept
    {
      return m_noAbortFlag.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the current quiet flag
     * \return the current quiet flag
     */
    [[nodiscard]]
    bool is_quiet() const noexcept
    {
      return m_quietFlag.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the current verbosity flag
     * \return the current verbosity flag
     */
    [[nodiscard]]
    bool is_verbose() const noexcept
    {
      return m_verboseFlag.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the name supplied to this test_resource at construction
     * \return the name of this test_resource
     */
    [[nodiscard]]
    std::string_view name() const noexcept
    {
      return m_name;
    }

    /**
     * \brief Returns the pointer to the upstream memory_resource supplied
     *        to this test_resource at construction
     * \return pointer to the upstream memory_resource
     */
    [[nodiscard]]
    std::pmr::memory_resource* upstream_resource() const noexcept
    {
      return m_upstream;
    }

    /**
     * \brief Returns the pointer to the last memory block successfully
     *        allocated by this test_resource
     * \return pointer to the last allocated memory block
     */
    [[nodiscard]]
    void* last_allocated_address() const noexcept
    {
      return m_lastAllocatedAddress.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the requested number of bytes of the last memory
     *        block successfully allocated by this test_resource
     * \return requested number of bytes of the last memory block
     */
    [[nodiscard]]
    std::size_t last_allocated_bytes() const noexcept
    {
      return m_lastAllocatedNumBytes.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the requested alignment of the last memory block
     *        successfully allocated by this test_resource
     * \return the requested alignment of the last memory block
     */
    [[nodiscard]]
    std::size_t last_allocated_alignment() const noexcept
    {
      return m_lastAllocatedAlignment.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the pointer to the last memory block successfully
     *        deallocated by this test_resource
     * \return pointer to the last deallocated memory block
     */
    [[nodiscard]]
    void* last_deallocated_address() const noexcept
    {
      return m_lastDeallocatedAddress.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the requested number of bytes of the last memory block
     *        successfully deallocated by this test_resource
     * \return number of bytes of the last requested memory block
     */
    [[nodiscard]]
    std::size_t last_deallocated_bytes() const noexcept
    {
      return m_lastDeallocatedNumBytes.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the requested alignment of the last memory block successfully
     *        deallocated by this test_resource
     * \return the requested alignment of the last deallocated memory block
     */
    [[nodiscard]]
    std::size_t last_deallocated_alignment() const noexcept
    {
      return m_lastDeallocatedAlignment.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the total number of allocations requested from this test_resource
     * \return total number of allocations
     * \note This number includes failed allocations too.
     */
    [[nodiscard]]
    long long allocations() const noexcept
    {
      return m_allocations.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of total deallocations requested from this test_resource
     * \return total number of deallocations
     * \note This number includes failed deallocations too.
     */
    [[nodiscard]]
    long long deallocations() const noexcept
    {
      return m_deallocations.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of memory blocks still allocated by this test_resource
     * \return the number of still used memory blocks
     */
    [[nodiscard]]
    long long blocks_in_use() const noexcept
    {
      return m_blocksInUse.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the largest number of memory blocks allocated at
     *        any given time by this test_resource
     * \return the largest number of allocated memory blocks
     */
    [[nodiscard]]
    long long max_blocks() const noexcept
    {
      return m_maxBlocks.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the total number of memory blocks ever
     *        successfully allocated by this test_resource
     * \return total number of successfully allocated memory blocks
     */
    [[nodiscard]]
    long long total_blocks() const noexcept
    {
      return m_totalBlocks.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of buffer overruns and underruns detected
     *        by this test_resource.
     * \return the number of buffer overruns and underruns
     */
    [[nodiscard]]
    long long bounds_errors() const noexcept
    {
      return m_boundsErrors.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of mismatched deallocation size and
     *        alignment parameters detected by this test_resource
     * \return the number of mismatched deallocation size and
     *         alignment parameters
     */
    [[nodiscard]]
    long long bad_deallocate_params() const noexcept
    {
      return m_badDeallocateParams.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of mismatched deallocations detected by
     *       this test_resource
     * \return the number of mismatched deallocations
     * \note Mismatched deallocations are deallocation attempts of memory
     *       blocks not obtained from this test_resource.
     */
    [[nodiscard]]
    long long mismatches() const noexcept
    {
      return m_mismatches.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the number of bytes currently allocated by this test_resource
     * \return the number of bytes currently allocated
     */
    [[nodiscard]]
    long long bytes_in_use() const noexcept
    {
      return m_bytesInUse.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the largest number of bytes allocated at
     *        any given time by this test_resource
     * \return the largest number of allocated bytes
     */
    [[nodiscard]]
    long long max_bytes() const noexcept
    {
      return m_maxBytes.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the total number of bytes ever successfully
     *        allocated by this test_resource
     * \return total number of successfully allocated bytes
     */
    [[nodiscard]]
    long long total_bytes() const noexcept
    {
      return m_totalBytes.load(std::memory_order_relaxed);
    }

    /**
     * \brief Returns the reporter assigned to the test_resource
     * \return total number of successfully allocated bytes
     */
    [[nodiscard]]
    test_resource_reporter* reporter() const noexcept
    {
      return m_reporter;
    }

    /**
     * \brief Detects an error
     * \return false if mismatches() and bounds_errors() and bad_deallocate_params() all return zero
     *         and true otherwise
     */
    [[nodiscard]]
    bool has_errors() const noexcept
    {
      return mismatches() != 0LL ||
        bounds_errors() != 0LL ||
        bad_deallocate_params() != 0LL;
    }

    /**
     * \brief Detects any allocation
     * \return true if blocks_in_use() or bytes_in_use() are greater than zero and false otherwise
     * \note if either is non‐zero both are non‐zero
     */
    [[nodiscard]]
    bool has_allocations() const noexcept
    {
      return blocks_in_use() > 0LL || bytes_in_use() > 0LL;
    }

    /**
     * \brief Get the status of test_resource
     * \return 0 - if this test_resource has detected no errors and
     *             it does not currently have any active allocations
     *             (no memory leaks).
     *         The number of detected errors if there are any.
     *         ‐1 - if there are active allocations (but no errors).
     */
    [[nodiscard]]
    long long status() const
    {
      std::lock_guard<std::mutex> guard{ m_lock };

      long long numErrors(mismatches());
      numErrors += bounds_errors();
      numErrors += bad_deallocate_params();

      if (numErrors > 0LL)
      {
        return numErrors;
      }

      if (has_allocations())
      {
        return -1; //inidcation of memory leak
      }

      return 0; //success
    }

    void print() const
    {
      std::lock_guard<std::mutex> guard{ m_lock };
      m_reporter->report_print(*this);
    }

    void release() noexcept
    {
      std::lock_guard<std::mutex> guard{ m_lock };

      if (is_verbose())
      {
        m_reporter->report_print(*this);
      }

      m_list->clear(m_upstream);
      m_upstream->deallocate(m_list,
        sizeof(detail::test_resource_list),
        alignof(detail::test_resource_list));

      if (!is_quiet())
      {
        m_reporter->report_release(*this);
      }
    }

  private:
    [[nodiscard]]
    const detail::test_resource_list* test_resource_list() const
    {
      return m_list;
    }

    template<std::size_t Align>
    void* do_allocate_impl(std::size_t bytes, long long allocation_index)
    {
      auto* header = static_cast<detail::aligned_header<Align>*>(m_upstream->allocate(
        detail::aligned_header_size_v<Align> + bytes + detail::padding_size, Align));

      if (!header)
      {
        // We cannot satisfy this request. Throw 'std::bad_alloc'.
        throw std::bad_alloc();
      }

      m_lastAllocatedNumBytes.store(static_cast<long long>(bytes), std::memory_order_relaxed);
      m_lastAllocatedAlignment.store(static_cast<long long>(Align), std::memory_order_relaxed);

      //initialize header padding + additional padding before the payload
      memset(&header->m_object.m_padding,
        std::to_integer<unsigned char>(detail::padded_memory_byte),
        reinterpret_cast<std::byte*>(header + 1) - reinterpret_cast<std::byte*>(&header->m_object.m_padding));

      //initialize padding after the payload
      memset(reinterpret_cast<std::byte*>(header + 1) + bytes,
        std::to_integer<unsigned char>(detail::padded_memory_byte),
        detail::padding_size);

      header->m_object.m_bytes = bytes;
      header->m_object.m_alignment = Align;
      header->m_object.m_magic_number = detail::allocated_memory_pattern;
      header->m_object.m_index = allocation_index;

      m_blocksInUse.fetch_add(1LL, std::memory_order_relaxed);
      if (max_blocks() < blocks_in_use())
      {
        m_maxBlocks.store(blocks_in_use(), std::memory_order_relaxed);
      }
      m_totalBlocks.fetch_add(1LL, std::memory_order_relaxed);

      m_bytesInUse.fetch_add(static_cast<long long>(bytes), std::memory_order_relaxed);
      if (max_bytes() < bytes_in_use())
      {
        m_maxBytes.store(bytes_in_use(), std::memory_order_relaxed);
      }
      m_totalBytes.fetch_add(static_cast<long long>(bytes), std::memory_order_relaxed);

      header->m_object.m_address = m_list->add_block(allocation_index, m_upstream);
      header->m_object.m_pmr = this;

      void* address = ++header;

      m_lastAllocatedAddress.store(address, std::memory_order_relaxed);

      if (is_verbose())
      {
        m_reporter->report_allocation(*this);
      }

      return address;
    }

    [[nodiscard]]
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
      std::lock_guard<std::mutex> guard(m_lock);
      const auto allocation_index = m_allocations.fetch_add(1, std::memory_order_relaxed);

      if (0LL <= allocation_limit())
      {
        if (0LL > m_allocationLimit.fetch_add(-1LL, std::memory_order_relaxed) - 1LL)
        {
          throw test_resource_exception(this, bytes, alignment);
        }
      }

      if (0U == alignment)
      {
        // Choose natural alignment for `bytes`
        alignment = ((bytes ^ (bytes - 1U)) >> 1U) + 1U;
        if (alignment > detail::max_natural_alignment)
        {
          alignment = detail::max_natural_alignment;
        }
      }

      if (!detail::is_power_of_two(alignment))
      {
        throw test_resource_exception(this, bytes, alignment);
      }

      switch (alignment)
      {
      case 1U:
        return do_allocate_impl<1U>(bytes, allocation_index);
      case 2U:
        return do_allocate_impl<2U>(bytes, allocation_index);
      case 4U:
        return do_allocate_impl<4U>(bytes, allocation_index);
      case 8U:
        return do_allocate_impl<8U>(bytes, allocation_index);
      case 16U:
        return do_allocate_impl<16U>(bytes, allocation_index);
      case 32U:
        return do_allocate_impl<32U>(bytes, allocation_index);
      case 64U:
        return do_allocate_impl<64U>(bytes, allocation_index);
      case 128U:
        return do_allocate_impl<128U>(bytes, allocation_index);
      case 256U:
        return do_allocate_impl<256U>(bytes, allocation_index);
      case 512U:
        return do_allocate_impl<512U>(bytes, allocation_index);
      case 1024U:
        return do_allocate_impl<1024U>(bytes, allocation_index);
      case 2048U:
        return do_allocate_impl<2048U>(bytes, allocation_index);
      case 4096U:
        return do_allocate_impl<4096U>(bytes, allocation_index);
      default:
        // TODO: let data_cache_line_size be a default alignment value
        // return do_allocate_impl<64U>(bytes, allocation_index);
        throw test_resource_exception(this, bytes, alignment);
      }
    }

    template<size_t Align>
    void do_deallocate_impl(void* p, std::size_t bytes)
    {
      auto* header = static_cast<detail::aligned_header<Align>*>(p) - 1;

      bool miscError = false;
      bool paramError = false;

      std::size_t size = 0U;

      // The following checks are done deliberately in the order shown to avoid a
      // possible bus error when attempting to read a misaligned 64-bit integer,
      // which can happen if an invalid address is passed to this method. If the
      // address of the memory being deallocated is misaligned, it is very likely
      // that 'm_magicNumber' will not match the expected value, and so we will
      // skip the reading of 'm_bytes_' (a 64-bit integer).
      if ((detail::allocated_memory_pattern != header->m_object.m_magic_number) ||
          (this != header->m_object.m_pmr))
      {
        miscError = true;
      }
      else
      {
        size = header->m_object.m_bytes;
      }

      // If there is evidence of corruption, this memory may have already been
      // freed.  On some platforms (but not others), the 'free' function will
      // scribble freed memory. To get uniform behavior for test drivers, we
      // deliberately don't check over/underruns if 'miscError' is 'true'.
      int overrunBy = 0;
      int underrunBy = 0;

      if (!miscError)
      {
        // Check the padding before the segment. Go backwards so we will
        // report the trashed byte nearest the segment.
        std::byte* pcBegin = static_cast<std::byte*>(p) - 1;
        auto* pcEnd = reinterpret_cast<std::byte*>(&header->m_object.m_padding);

        for (std::byte* pc = pcBegin; pcEnd <= pc; --pc)
        {
          if (detail::padded_memory_byte != *pc)
          {
            underrunBy = static_cast<int>(pcBegin + 1 - pc);
            break;
          }
        }

        if (!underrunBy)
        {
          // Check the padding after the segment.
          std::byte* tail = static_cast<std::byte*>(p) + size;
          pcBegin = tail;
          pcEnd = tail + detail::padding_size;
          for (std::byte* pc = pcBegin; pc < pcEnd; ++pc)
          {
            if (detail::padded_memory_byte != *pc)
            {
              overrunBy = static_cast<int>(pc + 1 - pcBegin);
              break;
            }
          }
        }

        if (bytes != size || Align != header->m_object.m_alignment)
        {
          paramError = true;
        }
      }

      // Now check for corrupted memory block and cross allocation.
      if (!miscError && !overrunBy && !underrunBy && !paramError)
      {
        m_upstream->deallocate(m_list->remove_block(header->m_object.m_address), sizeof(detail::block), alignof(detail::block));
      }
      else
      { // Any error, count it, report it
        if (miscError)
        {
          m_mismatches.fetch_add(1LL, std::memory_order_relaxed);
        }
        if (paramError)
        {
          m_badDeallocateParams.fetch_add(1LL, std::memory_order_relaxed);
        }
        if (overrunBy || underrunBy) {
          m_boundsErrors.fetch_add(1LL, std::memory_order_relaxed);
        }

        if (is_quiet())
        {
          return;
        }

        m_reporter->report_invalid_memory_block(
          *this,
          bytes,
          Align,
          underrunBy,
          overrunBy);

        if (is_no_abort())
        {
          return;
        }

        std::abort();
      }

      // At this point we know (almost) for sure that the memory block is
      // currently allocated from this object.  We now proceed to update our
      // statistics, stamp the block's header as deallocated, scribble over its
      // payload, and give it back to the underlying allocator supplied at
      // construction. In verbose mode, we also report the deallocation event to
      // 'outputSteam'.
      m_lastDeallocatedNumBytes.store(static_cast<long long>(size), std::memory_order_relaxed);
      m_lastDeallocatedAlignment.store(static_cast<long long>(Align), std::memory_order_relaxed);

      m_blocksInUse.fetch_add(-1LL, std::memory_order_relaxed);
      m_bytesInUse.fetch_add(-static_cast<long long>(size), std::memory_order_relaxed);

      header->m_object.m_magic_number = detail::deallocated_memory_pattern;
      memset(p, static_cast<int>(detail::scribbled_memory_byte), size);

      if (is_verbose())
      {
        m_reporter->report_deallocation(*this);
      }

      m_upstream->deallocate(header, detail::aligned_header_size_v<Align> + size + detail::padding_size, Align);

      // the deallocation via upstream may modify the magicnumber and data in user area
      //header->m_object.m_magic_number = detail::deallocated_memory_pattern;
      //memset(p, static_cast<int>(detail::scribbled_memory_byte), size);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
    {
      std::lock_guard<std::mutex> guard(m_lock);

      m_deallocations.fetch_add(1LL, std::memory_order_relaxed);
      m_lastDeallocatedAddress.store(p, std::memory_order_relaxed);

      if (!p)
      {
        if (0U != bytes)
        {
          m_badDeallocateParams.fetch_add(1LL, std::memory_order_relaxed);
          if (!is_quiet())
          {
            m_reporter->report_log_msg("*** Freeing a nullptr using non-zero size (%zu) with alignment (%zu). ***\n",
              bytes,
              alignment);

            if (!is_no_abort())
            {
              std::abort();
            }
          }
        }
        else
        {
          m_lastDeallocatedNumBytes.store(0U, std::memory_order_relaxed);
          m_lastDeallocatedAlignment.store(alignment, std::memory_order_relaxed);
        }
        return;
      }

      if (0U == alignment)
      {
        // Choose natural alignment for `bytes`
        alignment = ((bytes ^ (bytes - 1U)) >> 1U) + 1U;
        if (alignment > detail::max_natural_alignment)
        {
          alignment = detail::max_natural_alignment;
        }
      }

      if (!detail::is_power_of_two(alignment))
      {
        throw test_resource_exception(this, bytes, alignment);
      }

      switch (alignment)
      {
      case 1U:
        return do_deallocate_impl<1U>(p, bytes);
      case 2U:
        return do_deallocate_impl<2U>(p, bytes);
      case 4U:
        return do_deallocate_impl<4U>(p, bytes);
      case 8U:
        return do_deallocate_impl<8U>(p, bytes);
      case 16U:
        return do_deallocate_impl<16U>(p, bytes);
      case 32U:
        return do_deallocate_impl<32U>(p, bytes);
      case 64U:
        return do_deallocate_impl<64U>(p, bytes);
      case 128U:
        return do_deallocate_impl<128U>(p, bytes);
      case 256U:
        return do_deallocate_impl<256U>(p, bytes);
      case 512U:
        return do_deallocate_impl<512U>(p, bytes);
      case 1024U:
        return do_deallocate_impl<1024U>(p, bytes);
      case 2048U:
        return do_deallocate_impl<2048U>(p, bytes);
      case 4096U:
        return do_deallocate_impl<4096U>(p, bytes);
      default:
        // TODO: let data_cache_line_size be a default alignment value
        // return do_deallocate_impl<64U>(p, bytes);
        throw test_resource_exception(this, bytes, alignment);
      }
    }

    [[nodiscard]]
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
      return this == &other;
    }

    mutable std::mutex m_lock{};
    std::string_view m_name{};

    std::atomic_bool m_noAbortFlag{ false };
    std::atomic_bool m_quietFlag{ false };
    std::atomic_bool m_verboseFlag{ false };
    std::atomic_llong m_allocationLimit{ -1LL };

    std::atomic_llong m_allocations{ 0LL };
    std::atomic_llong m_deallocations{ 0LL };
    std::atomic_llong m_blocksInUse{ 0LL };
    std::atomic_llong m_maxBlocks{ 0LL };
    std::atomic_llong m_totalBlocks{ 0LL };
    std::atomic_llong m_boundsErrors{ 0LL };
    std::atomic_llong m_badDeallocateParams{ 0LL };
    std::atomic_llong m_mismatches{ 0LL };

    std::atomic_llong m_bytesInUse{ 0LL };
    std::atomic_llong m_maxBytes{ 0LL };
    std::atomic_llong m_totalBytes{ 0LL };

    std::atomic<void*> m_lastAllocatedAddress{ nullptr };
    std::atomic<void*> m_lastDeallocatedAddress{ nullptr };

    std::atomic_size_t m_lastAllocatedNumBytes{ 0U };
    std::atomic_size_t m_lastDeallocatedNumBytes{ 0U };

    std::atomic_size_t m_lastAllocatedAlignment{ 0U };
    std::atomic_size_t m_lastDeallocatedAlignment{ 0U };

    detail::test_resource_list* m_list{ nullptr };

    test_resource_reporter* m_reporter{ nullptr };

    //upstream resource from which to allocate
    std::pmr::memory_resource* m_upstream = std::pmr::get_default_resource();
  };

  /**
   * \brief The default resource guard is a simple RAII class that supports
   *        installing a new default polymorphic memory resource and then restoring
   *        of the original default polymorphic memory resource in its destructor.
   */
  class [[maybe_unused]] default_resource_guard final
  {
  public:
    explicit default_resource_guard(std::pmr::memory_resource* newDefault) noexcept
    {
      if (newDefault)
      {
        m_oldDefault = std::pmr::set_default_resource(newDefault);
      }
    }

    default_resource_guard(const default_resource_guard&) = delete;
    default_resource_guard& operator=(const default_resource_guard&) = delete;

    ~default_resource_guard() noexcept
    {
      // if m_oldDefault is nullPtr, the set_default_resource sets
      // a std::pmr::new_delete_resource as default one
      std::pmr::set_default_resource(m_oldDefault);
    }

  private:
    std::pmr::memory_resource* m_oldDefault{ nullptr };
  };

  template<typename F>
  void exception_test_loop(test_resource& tr, F&& f)
  {
    const auto orig_allocation_limit = tr.allocation_limit();

    for (long long exceptionCounter = 0LL; true; ++exceptionCounter)
    {
      try
      {
        tr.set_allocation_limit(exceptionCounter);
        std::invoke(std::forward<decltype(f)>(f), tr);
        tr.set_allocation_limit(orig_allocation_limit);
        return;
      }
      catch (const test_resource_exception& e)
      {
        tr.set_allocation_limit(orig_allocation_limit);

        if (e.originating_resource() != &tr)
        {
          tr.reporter()->report_log_msg(
            "  *** test_resource_exception from unexpected test resource: %p %.*s ***\n",
            static_cast<void*>(e.originating_resource()),
            static_cast<int>(e.originating_resource()->name().length()),
            e.originating_resource()->name().data());
          throw;
        }

        if (tr.is_verbose())
        {
          tr.reporter()->report_log_msg(
            "  *** test_resource_exception: alloc limit = %lld, last alloc size = %zu, align = %zu ***\n",
            exceptionCounter,
            e.size(),
            e.alignment());
        }
      }
    }
  }

  inline
  const detail::test_resource_list*
  test_resource_reporter::test_resource_list(const test_resource& tr) noexcept
  {
    return tr.test_resource_list();
  }

  inline void detail::stream_test_resource_reporter::do_report_allocation(const test_resource& tr)
  {
    m_stream << "test_resource";

    if (!tr.name().empty())
    {
      m_stream << " " << tr.name();
    }

    auto* address = tr.last_allocated_address();
    const auto alignment = tr.last_allocated_alignment();
    const auto bytes = tr.last_allocated_bytes();
    const auto* header = detail::get_header(address, alignment);

    if (header)
    {
      const auto allocation_index = header->m_index;

      m_stream << " [" << allocation_index << "]: Allocated "
        << bytes << " byte" << (bytes == 1U ? "" : "s")
        << " (aligned " << alignment << ") at "
        << formater_type::addr2str(address) << '.';
    }

    m_stream << std::endl;
  }

  inline void detail::stream_test_resource_reporter::do_report_deallocation(const test_resource& tr)
  {
    m_stream << "test_resource";

    if (!tr.name().empty())
    {
      m_stream << ' ' << tr.name();
    }

    auto* address = tr.last_deallocated_address();
    const auto alignment = tr.last_deallocated_alignment();
    const auto bytes = tr.last_deallocated_bytes();
    const auto* header = detail::get_header(address, alignment);

    if (header)
    {
      const auto allocation_index = header->m_index;

      m_stream << " [" << allocation_index << "]: Deallocated "
        << bytes << " byte" << (bytes == 1U ? "" : "s")
        << " (aligned " << alignment << ") at "
        << formater_type::addr2str(address) << '.';
    }

    m_stream << std::endl;
  }

  inline void detail::stream_test_resource_reporter::do_report_invalid_memory_block(
    const test_resource& tr,
    std::size_t deallocatedBytes,
    std::size_t deallocatedAlignment,
    int underrunBy,
    int overrunBy)
  {
    auto* payload = tr.last_deallocated_address();
    auto* head = get_header(payload, deallocatedAlignment);
    const auto* allocator = static_cast<const std::pmr::memory_resource*>(&tr);

    const auto magicNumber = head->m_magic_number;
    const auto numBytes = head->m_bytes;
    const auto alignment = head->m_alignment;

    if (allocated_memory_pattern != magicNumber)
    {
      if (deallocated_memory_pattern == magicNumber)
      {
        m_stream << "*** Deallocating previously deallocated memory at "
          << formater_type::addr2str(payload) << ". ***\n";
      }
      else
      {
        const auto prev_fill = m_stream.fill();
        const auto prev_width = m_stream.width();
        m_stream << "*** Invalid magic number "
          << std::hex << std::showbase << std::setw(8U) << std::setfill('0')
          << magicNumber
          << std::setfill(prev_fill) << std::setw(prev_width) << std::noshowbase << std::dec
          << " at address " << formater_type::addr2str(payload) << ". ***\n";
      }
    }
    else
    {
      if (numBytes <= 0U)
      {
        m_stream << "*** Invalid (non-positive) byte count " << numBytes
          << " at address " << formater_type::addr2str(payload) << ". *** \n";
      }
      if (deallocatedBytes != head->m_bytes)
      {
        m_stream << "*** Freeing segment at " << formater_type::addr2str(payload)
          << " using wrong size (" << deallocatedBytes << " vs. " << numBytes  << "). ***\n";
      }
      if (deallocatedAlignment != head->m_alignment)
      {
        m_stream << "*** Freeing segment at " << formater_type::addr2str(payload)
          << " using wrong alignment (" << deallocatedAlignment << " vs. " << alignment << "). ***\n";
      }
      if (allocator != head->m_pmr)
      {
        m_stream << "*** Freeing segment at " << formater_type::addr2str(payload)
          << " from wrong allocator. ***\n";
      }
      if (underrunBy)
      {
        m_stream << "*** Memory corrupted at " << underrunBy
          << " bytes before " << numBytes << " byte segment at "
          << formater_type::addr2str(payload) << ". ***\n";

        m_stream << "Pad area before user segment:\n";
        m_stream << formater_type::mem2str(
          &head->m_padding,
          static_cast<std::byte*>(payload) - reinterpret_cast<std::byte*>(&head->m_padding));
      }
      if (overrunBy)
      {
        m_stream << "*** Memory corrupted at " << overrunBy << " bytes after "
          << numBytes << " byte segment at " << formater_type::addr2str(payload) << ". ***\n";

        m_stream << "Pad area after user segment:\n";
        m_stream << formater_type::mem2str(static_cast<std::byte*>(payload) + numBytes, padding_size);
      }
    }

    m_stream << "Header + Padding:\n"
      << formater_type::mem2str(head, static_cast<std::byte*>(payload) - reinterpret_cast<std::byte*>(head));
    //let print 64 bytes when the header is "corrupted"
    m_stream << "User segment:\n" << formater_type::mem2str(payload, std::min<std::size_t>(64U, numBytes));
  }

  inline void detail::stream_test_resource_reporter::do_report_release(const test_resource& tr)
  {
    if (tr.has_allocations())
    {
      m_stream << "MEMORY_LEAK";
      if (!tr.name().empty())
      {
        m_stream << " from " << tr.name();
      }
      m_stream <<
        ":\n   Number of blocks in use = " << tr.blocks_in_use() <<
        "\n   Number of bytes in use = " << tr.bytes_in_use() << std::endl;

      if (!tr.is_no_abort())
      {
        std::abort();
      }
    }
  }

  inline void detail::stream_test_resource_reporter::do_report_print(const test_resource& tr)
  {
    m_stream <<
      "\n======================================================"
      "\n  TEST RESOURCE " << (!tr.name().empty() ? string_type(tr.name()) + " STATE" : "STATE") <<
      "\n------------------------------------------------------";

    const auto prev_flags = m_stream.flags();
    const auto prev_width = m_stream.width();
    m_stream <<
      "\n        Category    Blocks          Bytes"
      "\n        --------    ------          -----"
      "\n          IN USE    " << std::left << std::setw(16U) << tr.blocks_in_use() << std::setw(prev_width) << tr.bytes_in_use() <<
      "\n             MAX    " << std::setw(16U) << tr.max_blocks() << std::setw(prev_width) << tr.max_bytes() <<
      "\n           TOTAL    " << std::setw(16U) << tr.total_blocks() << std::setw(prev_width) << tr.total_bytes() <<
      "\n      MISMATCHES    " << tr.mismatches() <<
      "\n   BOUNDS ERRORS    " << tr.bounds_errors() <<
      "\n   PARAM. ERRORS    " << tr.bad_deallocate_params() <<
      "\n--------------------------------------------------\n";
    m_stream.setf(prev_flags);

    const auto* list = test_resource_list(tr);
    if (!list->empty())
    {
      m_stream << " Indices of Outstanding Memory Allocations:\n ";

      const auto* mblock = list->m_head;

      while (mblock)
      {
        // Prints the indices of max 8 'block' objects
        for (int i = 0; i < 8 && mblock; ++i)
        {
          m_stream << "  " << mblock->m_index;
          mblock = mblock->m_next;
        }
        m_stream << "\n ";
      }
    }

    m_stream.flush();
  }

  inline void detail::stream_test_resource_reporter::do_report_log_msg(const char* format, va_list args)
  {
    m_stream << formater_type::msg2str(format, args);
  }

  /**
   * \brief The test_resource_monitor works in tandem with test_resource to observe changes
   *        (or lack of changes) in the statistics collected by a test_resource.
   * \note  Monitored statistics are based on number of memory blocks and do not depend
   *        on the number of bytes in those allocated blocks.
   */
  class test_resource_monitor
  {
  public:
    explicit test_resource_monitor(const test_resource& monitored) noexcept
      : m_initialInUse(monitored.blocks_in_use())
      , m_initialMax(monitored.max_blocks())
      , m_initialTotal(monitored.total_blocks())
      , m_monitored(monitored)
    {
    }

    // To avoid binding the const ref arg to a temporary (above).
    explicit test_resource_monitor(test_resource&&) = delete;

    test_resource_monitor(const test_resource_monitor&) = delete;
    test_resource_monitor& operator=(const test_resource_monitor&) = delete;

    void reset() noexcept
    {
      m_initialInUse = m_monitored.blocks_in_use();
      m_initialMax = m_monitored.max_blocks();
      m_initialTotal = m_monitored.total_blocks();
    }

    [[nodiscard]]
    bool is_in_use_down() const noexcept
    {
      return m_monitored.blocks_in_use() < m_initialInUse;
    }

    [[nodiscard]]
    bool is_in_use_same() const noexcept
    {
      return m_monitored.blocks_in_use() == m_initialInUse;
    }

    [[nodiscard]]
    bool is_in_use_up() const noexcept
    {
      return m_monitored.blocks_in_use() > m_initialInUse;
    }

    [[nodiscard]]
    bool is_max_same() const noexcept
    {
      return m_initialMax == m_monitored.max_blocks();
    }

    [[nodiscard]]
    bool is_max_up() const noexcept
    {
      return m_monitored.max_blocks() != m_initialMax;
    }

    [[nodiscard]]
    bool is_total_same() const noexcept
    {
      return m_monitored.total_blocks() == m_initialTotal;
    }

    [[nodiscard]]
    bool is_total_up() const noexcept
    {
      return m_monitored.total_blocks() != m_initialTotal;
    }

    [[nodiscard]]
    long long delta_blocks_in_use() const noexcept
    {
      return m_monitored.blocks_in_use() - m_initialInUse;
    }

    [[nodiscard]]
    long long delta_max_blocks() const noexcept
    {
      return m_monitored.max_blocks() - m_initialMax;
    }

    [[nodiscard]]
    long long delta_total_blocks() const noexcept
    {
      return m_monitored.total_blocks() - m_initialTotal;
    }

  private:
    long long            m_initialInUse;
    long long            m_initialMax;
    long long            m_initialTotal;
    const test_resource& m_monitored;
  };

  // C++20 enhancements of std::pmr::polymorphic_allocator
  // The implementation of P0339R6 proposal:
  // "polymorphic_allocator<> as a vocabulary type"
  // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0339r6.pdf
  // https://en.cppreference.com/w/cpp/memory/polymorphic_allocator
  template<typename T = std::byte>
  class polymorphic_allocator : public std::pmr::polymorphic_allocator<T>
  {
    template<typename>
    friend class stdx::pmr::polymorphic_allocator;

  public:
    using value_type = T;

    // simplification
    // just reuse the constructors of std::pmr::polymorphic_allocator<T>
    using std::pmr::polymorphic_allocator<T>::polymorphic_allocator;

    polymorphic_allocator& operator=(const polymorphic_allocator&) = delete;

    /**
     * \brief Allocates nbytes bytes of storage at specified alignment alignment using
     *       the underlying memory resource. Equivalent to return resource()->allocate(nbytes, alignment)
     * \param nbytes the number of bytes to allocate
     * \param alignment the alignment to use
     * \return A pointer to the allocated storage
     */
    [[nodiscard]]
    void* allocate_bytes(std::size_t nbytes, size_t alignment = detail::max_natural_alignment)
    {
      return this->resource()->allocate(nbytes, alignment);
    }

    /**
     * \brief Deallocates the storage pointed to by p, which must have been allocated from
     *        a std::pmr::memory_resource x that compares equal to *resource().
     *        using x.allocate(nbytes, alignment), typically through a call to
     *        allocate_bytes(nbytes, alignment).
     *        Equivalent to resource()->deallocate(p, nbytes, alignment);
     * \param p pointer to memory to deallocate
     * \param nbytes the number of bytes originally allocated
     * \param alignment the alignment originally allocated
     */
    void deallocate_bytes(void* p, std::size_t nbytes, std::size_t alignment = detail::max_natural_alignment)
    {
      return this->resource()->deallocate(p, nbytes, alignment);
    }

    /**
     * \brief Allocates storage for n objects of type U using the underlying memory resource.
     * \tparam U the type of object
     * \param n the number of objects to allocate storage for
     * \return A pointer to the allocated storage.
     * \note If std::numeric_limits<std::size_t>::max() / sizeof(U) < n, throws std::bad_array_new_length,
     *       otherwise equivalent to return static_cast<U*>(allocate_bytes(n * sizeof(U), alignof(U)) );
     */
    template<typename U>
    [[nodiscard]]
    U* allocate_object(std::size_t n = 1U)
    {
      return static_cast<U*>(allocate_bytes(n * sizeof(U), alignof(U)));
    }

    /**
     * \brief Deallocates the storage pointed to by p, which must have been allocated from
     *       a std::pmr::memory_resource x that compares equal to *resource(). using
     *       x.allocate(n*sizeof(U), alignof(U)), typically through a call to allocate_object<U>(n).
     *       Equivalent to deallocate_bytes(p, n*sizeof(U), alignof(U));
     * \tparam U the type of object
     * \param p pointer to memory to deallocate
     * \param n number of objects of type U the memory was for
     */
    template<typename U>
    void deallocate_object(U* p, std::size_t n = 1U)
    {
      deallocate_bytes(p, n * sizeof(U), alignof(U));
    }

    /**
     * \brief Allocates and constructs an object of type U.
     * \tparam U
     * \tparam Args
     * \param args the arguments to forward to the the constructor of U
     * \return A pointer to the allocated and constructed object.
     */
    template<typename U, typename... Args>
    [[nodiscard]]
    U* new_object(Args&&... args)
    {
      U* p = allocate_object<U>();
      try
      {
        construct(p, std::forward<Args>(args)...);
      }
      catch (...)
      {
        deallocate_object(p);
        throw;
      }
      return p;
    }

    /**
     * \brief Destroys the object of type U and deallocates storage allocated for it.
     * \tparam U the type of object
     * \param p pointer to the object to destroy and deallocate
     */
    template<typename U>
    void delete_object(U* p)
    {
      p->~U(); // MSVC C++17 Std doesn't implement destroy() method on top of std::polymorphic_allocator, instead of it
               // the std::destroy_at(p) can be utilized or direct call of type destructor.
      deallocate_object(p);
    }
  };
}

#endif