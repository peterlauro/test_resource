#include "memory_resource.h"

//  GTEST
#include <gtest/gtest.h>

#include <deque>
#include <string>

inline constexpr bool g_verbose = true;

class pstring_no_destructor
{
  public:
    using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_no_destructor(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(static_cast<char *>(m_allocator.allocate_bytes(m_length, 1U)))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  { // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_destruction__no_destructor)
#else
TEST(StdX_MemoryResource_test_resource, destruction__no_destructor)
#endif
{
  // MEMORY LEAK DETECTION
  {
     const bool verbose = g_verbose;
     stdx::pmr::test_resource tpmr("stage1", verbose );
     tpmr.set_no_abort(true);
     std::size_t strlength = 0U;
     {
       const pstring_no_destructor astring{ "foobar", &tpmr };
       EXPECT_EQ(astring.str(), "foobar");
       strlength = astring.size();
     }
     EXPECT_TRUE(tpmr.has_allocations());
     EXPECT_FALSE(tpmr.has_errors());
     EXPECT_EQ(tpmr.status(), -1LL); //memory leak
     EXPECT_EQ(static_cast<std::size_t>(tpmr.bytes_in_use()), strlength);
  } //test memory resource is destructed
}

class pstring_inconsistent_alignment
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_inconsistent_alignment(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(static_cast<char *>(m_allocator.allocate_bytes(m_length, 1U)))
  {
    strcpy(m_buffer, cstr);
  }

  ~pstring_inconsistent_alignment()
  {
    m_allocator.deallocate_bytes(m_buffer, m_length, 2U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_destruction__inconsistent_alignment)
#else
TEST(StdX_MemoryResource_test_resource, destruction__inconsistent_alignment)
#endif
{
  // WRONG ALIGNMENT AND BUFFER OVERRUN DETECTION
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage2", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    {
      const pstring_inconsistent_alignment astring{ "foobar", &tpmr };
      EXPECT_EQ(astring.str(), "foobar");
      strlength = astring.size();
    }
    EXPECT_TRUE(tpmr.has_allocations());
    EXPECT_TRUE(tpmr.has_errors());
    EXPECT_EQ(static_cast<std::size_t>(tpmr.bytes_in_use()), strlength);
    EXPECT_EQ(tpmr.bounds_errors(), 1LL);
    EXPECT_EQ(tpmr.bad_deallocate_params(), 1LL);
  } //test memory resource is destructed
}

class pstring_wrong_bytes_number
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_wrong_bytes_number(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  ~pstring_wrong_bytes_number()
  {
    m_allocator.deallocate_object(m_buffer, m_length);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_destruction__wrong_number_of_bytes)
#else
TEST(StdX_MemoryResource_test_resource, destruction__wrong_number_of_bytes)
#endif
{
  // WRONG NUMBER OF BYTES IN DEALLOCATE
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage3", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    {
      const pstring_wrong_bytes_number astring{ "foobar", &tpmr };
      EXPECT_EQ(astring.str(), "foobar");
      strlength = astring.size();
    }
    EXPECT_TRUE(tpmr.has_allocations());
    EXPECT_TRUE(tpmr.has_errors());
    EXPECT_EQ(static_cast<std::size_t>(tpmr.bytes_in_use()), strlength + 1U);
    EXPECT_EQ(tpmr.bad_deallocate_params(), 1LL);
  } //test memory resource is destructed
}

class pstring_correct_create_destroy
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_correct_create_destroy(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strcpy(m_buffer, cstr);
  }

  ~pstring_correct_create_destroy()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length};
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

TEST(StdX_MemoryResource_test_resource, create_destroy__correct)
{
  // SUCCESS OF CREATE/DESTROY
  {
    using namespace std::string_literals;
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage4", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    {
      const pstring_correct_create_destroy astring{ "foobar", &tpmr };
      EXPECT_EQ(astring.str(), "foobar"s);
      strlength = astring.size();
    }

    EXPECT_FALSE(tpmr.has_allocations());
    EXPECT_FALSE(tpmr.has_errors());
    EXPECT_EQ(tpmr.bytes_in_use(), 0LL);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.max_bytes()), strlength + 1U);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.total_bytes()), strlength + 1U);
  } //test memory resource is destructed
}

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_double_deallocation)
#else
TEST(StdX_MemoryResource_test_resource, double_deallocation)
#endif
{
  // DEALLOCATION OF ALREADY DEALLOCATED POINTER
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage4a", verbose);
    tpmr.set_no_abort(true);
    {
      const pstring_correct_create_destroy astring{ "foobar", &tpmr };
      //pstring_correct_create_destroy doesn't provide "suitable" copy constructor, with allocator
      const pstring_correct_create_destroy astring_copied{ astring};
      EXPECT_EQ(astring.str(), "foobar");
      EXPECT_EQ(astring_copied.str(), "foobar");
    } // destroy and deletes astring_copied (the buffer is cleared)
    // tries to destroy and deletes astring which works with pointer on the same buffer already destroy by ~astring_copied
    EXPECT_FALSE(tpmr.has_allocations());
    EXPECT_TRUE(tpmr.has_errors());
    EXPECT_EQ(tpmr.status(), 1LL);
    EXPECT_EQ(tpmr.bytes_in_use(), 0LL);
    EXPECT_EQ(tpmr.mismatches(), 1LL);
  } //test memory resource is destructed
}

class pstring_correct_copy_constructor
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_correct_copy_constructor(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  //copy constructor
  pstring_correct_copy_constructor(const pstring_correct_copy_constructor& other, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(other.m_length)
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1))
  {
    strncpy(m_buffer, other.m_buffer, m_length);
  }

  ~pstring_correct_copy_constructor()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

TEST(StdX_MemoryResource_test_resource, copy_construction__correct)
{
  // IMPLEMENTED A COPY CONSTRUCTOR
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource dpmr{"default", verbose};
    stdx::pmr::default_resource_guard dg(&dpmr);

    stdx::pmr::test_resource tpmr("stage5", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    std::size_t strlength_copied = 0U;
    {
      const pstring_correct_copy_constructor astring{ "foobar", &tpmr };
      //pstring_correct_create_destroy doesn't provide "suitable" copy constructor, with allocator
      const pstring_correct_copy_constructor astring_copied{ astring }; //string uses the default resource, dmpr
      EXPECT_EQ(astring.str(), "foobar");
      EXPECT_EQ(astring_copied.str(), "foobar");
      strlength = astring.size();
      strlength_copied = astring_copied.size();
    } // destroy and deletes astring_copied
    // destroy and deletes astring
    EXPECT_EQ(strlength, strlength_copied);

    EXPECT_FALSE(tpmr.has_allocations());
    EXPECT_FALSE(tpmr.has_errors());
    EXPECT_EQ(tpmr.status(), 0LL);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.max_bytes()), strlength + 1U);

    EXPECT_FALSE(dpmr.has_allocations());
    EXPECT_FALSE(dpmr.has_errors());
    EXPECT_EQ(dpmr.status(), 0LL);
    EXPECT_EQ(static_cast<std::size_t>(dpmr.max_bytes()), strlength_copied + 1U);
  } //memory resources are destructed
}

class pstring_wrong_assignment_operator
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_wrong_assignment_operator(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  //copy constructor
  pstring_wrong_assignment_operator(const pstring_wrong_assignment_operator& other, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(other.m_length)
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1))
  {
    strncpy(m_buffer, other.m_buffer, m_length);
  }

  //wrong copy assignment operator
  pstring_wrong_assignment_operator& operator=(const pstring_wrong_assignment_operator& rhs)
  {
    m_length = rhs.m_length;
    m_buffer = rhs.m_buffer;
    return *this;
  }

  ~pstring_wrong_assignment_operator()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_copy_assignment__incorrect)
#else
TEST(StdX_MemoryResource_test_resource, copy_assignment__incorrect)
#endif
{
  // WRONG ASSIGNMENT OPERATOR
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage6", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    std::size_t strlength_assigned = 0U;
    {
      const pstring_wrong_assignment_operator astring{ "foobar", &tpmr };
      pstring_wrong_assignment_operator astring_assigned{ "string", &tpmr };
      astring_assigned = astring;
      EXPECT_EQ(astring.str(), "foobar");
      EXPECT_EQ(astring_assigned.str(), "foobar");
      strlength = astring.size();
      strlength_assigned = astring_assigned.size();
    } // destroy and deletes astring_assigned
    // destroy and deletes astring  - problem (the same buffer)
    EXPECT_EQ(strlength, strlength_assigned);
    EXPECT_TRUE(tpmr.has_allocations());
    EXPECT_TRUE(tpmr.has_errors());
    EXPECT_EQ(tpmr.mismatches(), 1LL);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.bytes_in_use()), strlength + 1U);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.max_bytes()), 2U * (strlength + 1U));
    EXPECT_EQ(static_cast<std::size_t>(tpmr.total_bytes()), 2U * (strlength + 1U));
  } //test memory resource is destructed
}

class pstring_correct_assignment_operator
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_correct_assignment_operator(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  //copy constructor
  pstring_correct_assignment_operator(const pstring_correct_assignment_operator& other, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(other.m_length)
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1))
  {
    strncpy(m_buffer, other.m_buffer, m_length);
  }

  //copy assignment operator
  pstring_correct_assignment_operator& operator=(const pstring_correct_assignment_operator& rhs)
  {
    char* buff = m_allocator.allocate_object<char>(rhs.m_length + 1U); //create new buffer
    m_allocator.deallocate_object(m_buffer, m_length + 1U); //deallocate actual buffer
    m_buffer = buff;
    strncpy(m_buffer, rhs.m_buffer, m_length);
    m_length = rhs.m_length;
    return *this;
  }

  ~pstring_correct_assignment_operator()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {   // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

TEST(StdX_MemoryResource_test_resource, copy_assignment__correct)
{
  // IMPLEMENTED A COPY ASSIGNMENT OPERATOR
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage7", verbose);
    tpmr.set_no_abort(true);
    std::size_t strlength = 0U;
    std::size_t strlength_assigned = 0U;
    {
      const pstring_correct_assignment_operator astring{ "foobar", &tpmr };
      pstring_correct_assignment_operator astring_assigned{ "other string", &tpmr };
      strlength_assigned = astring_assigned.size();
      astring_assigned = astring;
      EXPECT_EQ(astring.str(), "foobar");
      EXPECT_EQ(astring_assigned.str(), "foobar");
      strlength = astring.size();
    } // destroy and deletes astring_assigned
    // destroy and deletes astring
    EXPECT_FALSE(tpmr.has_allocations());
    EXPECT_FALSE(tpmr.has_errors());
    EXPECT_EQ(tpmr.mismatches(), 0LL);
    EXPECT_EQ(tpmr.bytes_in_use(), 0LL);
    EXPECT_EQ(static_cast<std::size_t>(tpmr.max_bytes()), 2U * (strlength + 1U) + (strlength_assigned + 1U));
    EXPECT_EQ(static_cast<std::size_t>(tpmr.total_bytes()), 2U * (strlength + 1U) + (strlength_assigned + 1U));
  } //test memory resource is destructed
}

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_self_assignment__incorrect)
#else
TEST(StdX_MemoryResource_test_resource, self_assignment__incorrect)
#endif
{
  // SELF‐ASSIGNMENT TEST
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage7a", verbose);
    std::size_t strlength = 0U;
    tpmr.set_no_abort(true);
    {
      pstring_correct_assignment_operator astring{ "foobar", &tpmr };
      strlength = astring.size();
      astring = astring;
      EXPECT_NE(astring.str(), "foobar");
    }
    EXPECT_EQ(2U * (strlength + 1U), static_cast<std::size_t>(tpmr.max_bytes()));
  } //test memory resource is destructed
}

class pstring_fixed_self_assignment
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_fixed_self_assignment(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  //copy constructor
  pstring_fixed_self_assignment(const pstring_fixed_self_assignment& other, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(other.m_length)
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1))
  {
    strncpy(m_buffer, other.m_buffer, m_length);
  }

  //copy assignment operator
  pstring_fixed_self_assignment& operator=(const pstring_fixed_self_assignment& rhs)
  {
    if (this != std::addressof(rhs))
    {
      char* buff = m_allocator.allocate_object<char>(rhs.m_length + 1U); //create new buffer
      m_allocator.deallocate_object(m_buffer, m_length + 1U); //deallocate actual buffer
      m_buffer = buff;
      strncpy(m_buffer, rhs.m_buffer, m_length);
      m_length = rhs.m_length;
    }
    return *this;
  }

  ~pstring_fixed_self_assignment()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

TEST(StdX_MemoryResource_test_resource, self_assignment__correct)
{
  // SELF‐ASSIGNMENT FIXED
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tpmr("stage8", verbose);
    std::size_t strlength = 0U;
    tpmr.set_no_abort(true);
    {
      pstring_fixed_self_assignment astring{ "foobar", &tpmr };
      strlength = astring.size();
      astring = astring;
      EXPECT_EQ(astring.str(), "foobar");
    }
    EXPECT_EQ(strlength + 1U, static_cast<std::size_t>(tpmr.max_bytes()));
  } //test memory resource is destructed
}

TEST(StdX_MemoryResource_test_resource, move_constructor__incorrect)
{
  // NO MOVE-CONSTRUCTOR
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tr{"object", verbose};
    stdx::pmr::test_resource_monitor trm{ tr };
    tr.set_no_abort(true);
    stdx::pmr::test_resource dr{ "default", verbose };
    stdx::pmr::test_resource_monitor drm{ dr };
    stdx::pmr::default_resource_guard drg{ &dr };
    {
      pstring_correct_copy_constructor astring{ "foobar", &tr };
      EXPECT_TRUE(trm.is_total_up());
      EXPECT_EQ(trm.delta_blocks_in_use(), 1LL);
      trm.reset();
      //uses the default copy-constructor without allocator parameter
      pstring_correct_copy_constructor bstring{ std::move(astring) };
    }
    EXPECT_TRUE(trm.is_total_same());
    //the copy constructor was called and default test_resource was utilized
    EXPECT_FALSE(drm.is_total_same());
  }
}

class pstring_with_move_constructor
{
public:
  using allocator_type = stdx::pmr::polymorphic_allocator<>;

  pstring_with_move_constructor(const char *cstr, allocator_type allocator = {})
    : m_allocator(allocator)
    , m_length(strlen(cstr))
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1U))
  {
    strncpy(m_buffer, cstr, m_length);
  }

  //copy constructor
  pstring_with_move_constructor(const pstring_with_move_constructor& other, allocator_type allocator = {})
    : m_allocator(allocator) //don't propagate the other allocator on copy construction
    , m_length(other.m_length)
    , m_buffer(m_allocator.allocate_object<char>(m_length + 1))
  {
    strncpy(m_buffer, other.m_buffer, m_length);
  }

  //move constructor
  pstring_with_move_constructor(pstring_with_move_constructor&& other) noexcept
    : pstring_with_move_constructor(std::move(other), other.m_allocator) //propagate other allocator on regular move constructor
  {
  }

  pstring_with_move_constructor(pstring_with_move_constructor&& other, allocator_type allocator)
    : m_allocator(allocator) //don't propagate other allocator on extended move constructor
    , m_length(other.m_length)
  {
      if (m_allocator == other.m_allocator)
      {
          m_buffer = other.m_buffer;
      }
      else
      {
          m_buffer = m_allocator.allocate_object<char>(m_length + 1U); //create new buffer
          strcpy(m_buffer, other.m_buffer);
          other.m_allocator.deallocate_object(other.m_buffer, m_length + 1U);
      }
      other.m_length = static_cast<std::size_t>(-1);
      other.m_buffer = nullptr;
  }

  //copy assignment operator
  pstring_with_move_constructor& operator=(const pstring_with_move_constructor& rhs)
  {
    if (this != std::addressof(rhs))
    {
      char* buff = m_allocator.allocate_object<char>(rhs.m_length + 1U); //create new buffer
      m_allocator.deallocate_object(m_buffer, m_length + 1U); //deallocate actual buffer
      m_buffer = buff;
      strncpy(m_buffer, rhs.m_buffer, rhs.m_length);
      m_length = rhs.m_length;
    }
    return *this;
  }

  //move assigned operator
  pstring_with_move_constructor& operator=(pstring_with_move_constructor&& rhs)
  {
    if (m_allocator == rhs.m_allocator)
    {
      swap(rhs); // not-copying move
    }
    else
    {
      operator=(rhs); //rhs is lvalue -> call copy assignment operator
    }
    return *this;
  }

  ~pstring_with_move_constructor()
  {
    m_allocator.deallocate_object(m_buffer, m_length + 1U);
  }

  [[nodiscard]]
  allocator_type get_allocator() const
  {
    return m_allocator;
  }

  [[nodiscard]]
  std::string str() const
  {  // For sanity checks only.
    return { m_buffer, m_length };
  }

  [[nodiscard]]
  std::size_t size() const noexcept
  {
    return m_length;
  }

  [[nodiscard]]
  char* get_buffer()
  {
    return m_buffer;
  }

  [[nodiscard]]
  const char* get_buffer() const
  {
    return m_buffer;
  }

  void swap(pstring_with_move_constructor& other) noexcept
  {
    //don't swap allocators
    //swap can be done only on objects allocated with the same allocator
    using std::swap;
    swap(m_length, other.m_length);
    swap(m_buffer, other.m_buffer);
  }

private:
  allocator_type  m_allocator;
  size_t          m_length;
  char*           m_buffer;
};

using pstring_correct = pstring_with_move_constructor;

TEST(StdX_MemoryResource_test_resource, move_constructor__correct)
{
  // HAS MOVE-CONSTRUCTOR
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tr{ "object", verbose };
    stdx::pmr::test_resource_monitor trm{ tr };
    tr.set_no_abort(true);
    stdx::pmr::test_resource dr{ "default", verbose };
    stdx::pmr::test_resource_monitor drm{ dr };
    stdx::pmr::default_resource_guard drg{ &dr };
    {
      pstring_with_move_constructor astring{ "foobar", &tr };
      EXPECT_TRUE(trm.is_total_up());
      EXPECT_EQ(trm.delta_blocks_in_use(), 1LL);
      trm.reset();
      pstring_with_move_constructor bstring{ std::move(astring) };
    }
    EXPECT_TRUE(trm.is_total_same());
    EXPECT_TRUE(drm.is_total_same()); //no copy constructor was called
  }
}

TEST(StdX_MemoryResource_test_resource, copy_construction__empty_string)
{
    {
        const bool verbose = g_verbose;
        stdx::pmr::test_resource tr{ "object", verbose };
        stdx::pmr::test_resource_monitor trm{ tr };
        tr.set_no_abort(true);
        stdx::pmr::test_resource dr{ "default", verbose };
        stdx::pmr::test_resource_monitor drm{ dr };
        stdx::pmr::default_resource_guard drg{ &dr };
        {
            pstring_with_move_constructor astring{ "", &tr };
            EXPECT_TRUE(trm.is_total_up());
            EXPECT_EQ(trm.delta_blocks_in_use(), 1LL);
            trm.reset();
            pstring_with_move_constructor bstring{ astring };
        }
        EXPECT_TRUE(trm.is_total_same());
        EXPECT_FALSE(drm.is_total_same()); //copy constructor was called
    }
}

TEST(StdX_MemoryResource_exception_test_loop, allocations_detector)
{
  const bool verbose = g_verbose;
  stdx::pmr::test_resource tpmr{ "tester",verbose };
  tpmr.set_no_abort(true);
  const char *longstr = "A very very long string that allocates memory";

  stdx::pmr::exception_test_loop(tpmr,
  [longstr](std::pmr::memory_resource& pmrp) {
    std::pmr::deque<std::pmr::string> deq{ &pmrp };
      deq.emplace_back(longstr);
      deq.emplace_back(longstr);
      EXPECT_EQ(deq.size(), 2U);
    });
}

TEST(StdX_MemoryResource_default_resource_guard, with_test_resource_monitor)
{
  const bool verbose = g_verbose;
  stdx::pmr::test_resource tr{ "object", verbose };
  tr.set_no_abort(true);
  const std::pmr::string astring{
    "A very very long string that will hopefully allocate memory",
    &tr };
  stdx::pmr::test_resource dr{ "default", verbose };
  dr.set_no_abort(true);
  const stdx::pmr::test_resource_monitor drm{ dr };
  {
    stdx::pmr::default_resource_guard drg{ &dr };
    std::pmr::string string2{ astring, &tr };
  }
  EXPECT_TRUE(drm.is_total_same());
}

TEST(StdX_MemoryResource_aligned_header, size_and_alignment_verification)
{
  static_assert(sizeof(stdx::pmr::detail::aligned_header_base) == 64U);

    // alignment 1
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<1U>, stdx::pmr::detail::checked_alignment(1U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<1U>, 64U);

  // alignment 2
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<2U>, stdx::pmr::detail::checked_alignment(2U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<2U>, 64U);

  // alignment 4
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<4U>, stdx::pmr::detail::checked_alignment(4U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<4U>, 64U);

  // alignment 8
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<8U>, stdx::pmr::detail::checked_alignment(8U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<8U>, 64U);

  // alignment 16
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<16U>, stdx::pmr::detail::checked_alignment(16U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<16U>, 64U);

  // alignment 32
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<32U>, stdx::pmr::detail::checked_alignment(32U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<32U>, 64U);

  // alignment 64
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<64U>, stdx::pmr::detail::checked_alignment(64U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<64U>, 64U);

  // alignment 128
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<128U>, stdx::pmr::detail::checked_alignment(128U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<128U>, 128U);

  // alignment 256
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<256U>, stdx::pmr::detail::checked_alignment(256U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<256U>, 256U);

  // alignment 512
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<512U>, stdx::pmr::detail::checked_alignment(512U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<512U>, 512U);

  // alignment 1024
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<1024U>, stdx::pmr::detail::checked_alignment(1024U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<1024U>, 1024U);

  // alignment 2048
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<2048U>, stdx::pmr::detail::checked_alignment(2048U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<2048U>, 2048U);

  // alignment 4096
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<4096U>, stdx::pmr::detail::checked_alignment(4096U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<4096U>, 4096U);
}

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_overwrite_padding_before_payload)
#else
TEST(StdX_MemoryResource_test_resource, overwrite_padding_before_payload)
#endif
{
  const bool verbose = g_verbose;
  stdx::pmr::test_resource dr("default", verbose);
  dr.set_no_abort(true);
  {
    pstring_correct astring{ "foobar", &dr };
    auto* ptr = astring.get_buffer() - 4U;
    *ptr = 0x65; //write 'e' - overwrite the first padding area
  }
  EXPECT_EQ(dr.bounds_errors(), 1LL);
}

#ifdef __SANITIZE_ADDRESS__
TEST(StdX_MemoryResource_test_resource, DISABLED_overwrite_padding_after_payload)
#else
TEST(StdX_MemoryResource_test_resource, overwrite_padding_after_payload)
#endif
{
  const bool verbose = g_verbose;
  stdx::pmr::test_resource dr("default", verbose);
  dr.set_no_abort(true);
  {
    pstring_correct astring{ "foobar", &dr };
    auto* ptr = astring.get_buffer() + (astring.size() + 3U);
    *ptr = 0x65; //write 'e' - overwrite the tail padding area
  }
  EXPECT_EQ(dr.bounds_errors(), 1LL);
}

TEST(StdX_MemoryResource_test_resource, overwrite_padding_after_payload__output_to_file)
{
  const bool verbose = g_verbose;
  const char* filename("test_file.log");
  {
    std::filesystem::remove(filename);
    stdx::pmr::file_test_resource_reporter file_reporter(filename);
    stdx::pmr::test_resource dr("default", verbose, &file_reporter);
    dr.set_no_abort(true);
    {
      pstring_correct astring{ "foobar", &dr };
      auto* ptr = astring.get_buffer() + (astring.size() + 3U);
      *ptr = 0x65; //write 'e' - overwrite the tail padding area
    }
    EXPECT_EQ(dr.bounds_errors(), 1LL);
    EXPECT_TRUE(std::filesystem::exists(filename));
    EXPECT_FALSE(std::filesystem::is_empty(filename));
  }
  EXPECT_TRUE(std::filesystem::remove(filename));
}

TEST(StdX_MemoryResource_test_resource, overwrite_padding_after_payload__output_to_closed_file)
{
  const bool verbose = g_verbose;
  const char* filename("test_file.log");
  {
    std::filesystem::remove(filename);
    stdx::pmr::file_test_resource_reporter file_reporter(filename);
    stdx::pmr::test_resource dr("default", verbose, &file_reporter);
    file_reporter.close();
    dr.set_no_abort(true);
    {
      pstring_correct astring{ "foobar", &dr };
      auto* ptr = astring.get_buffer() + (astring.size() + 3U);
      *ptr = 0x65; //write 'e' - overwrite the tail padding area
    }
    EXPECT_EQ(dr.bounds_errors(), 1LL);
    EXPECT_TRUE(std::filesystem::exists(filename));
    EXPECT_TRUE(std::filesystem::is_empty(filename));
  }
  EXPECT_TRUE(std::filesystem::remove(filename));
}

TEST(StdX_MemoryResource_test_resource, overwrite_padding_after_payload__output_to_nonopen_file_reporter)
{
  const bool verbose = g_verbose;
  const char* filename("test_file.log");
  {
    std::filesystem::remove(filename);
    stdx::pmr::file_test_resource_reporter file_reporter;
    stdx::pmr::test_resource dr("default", verbose, &file_reporter);
    dr.set_no_abort(true);
    {
      pstring_correct astring{ "foobar", &dr };
      auto* ptr = astring.get_buffer() + (astring.size() + 3U);
      *ptr = 0x65; //write 'e' - overwrite the tail padding area
    }
    EXPECT_EQ(dr.bounds_errors(), 1LL);
    EXPECT_FALSE(std::filesystem::exists(filename));
  }
  EXPECT_FALSE(std::filesystem::remove(filename));
}
