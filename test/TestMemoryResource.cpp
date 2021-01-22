#include "memory_resource.h"

//  GTEST
#include <gtest/gtest.h>

#include <deque>
#include <fstream>
#include <string>
#include <new>

#define DYNAMIC_MEMORY_GUARD_DECL \
virtual void add_macro_DERIVED_DYNAMIC_MEMORY_HELPER_in_class_definition() const noexcept = 0;

#define DYNAMIC_MEMORY_GUARD_IMPL \
void add_macro_DERIVED_DYNAMIC_MEMORY_HELPER_in_class_definition() const noexcept override \
{ \
}

// definition of class specific allocation/deallocation functions
// https://en.cppreference.com/w/cpp/memory/new/operator_new
// https://en.cppreference.com/w/cpp/memory/new/operator_delete
#define DYNAMIC_MEMORY_HELPER(TYPE_NAME, RESOURCE_TYPE_NAME) \
[[nodiscard]] \
void* operator new(std::size_t size) \
{ \
    return RESOURCE_TYPE_NAME::get_memory_resource()->allocate(size, alignof(TYPE_NAME)); \
} \
 \
[[nodiscard]] \
void* operator new(std::size_t size, std::align_val_t align) \
{ \
    return RESOURCE_TYPE_NAME::get_memory_resource()->allocate(size, static_cast<std::size_t>(align)); \
} \
 \
void* operator new[](std::size_t) = delete; \
void* operator new[](std::size_t, std::align_val_t) = delete; \
 \
void operator delete(void* p) noexcept \
{ \
    RESOURCE_TYPE_NAME::get_memory_resource()->deallocate(p, sizeof(TYPE_NAME), alignof(TYPE_NAME)); \
} \
 \
void operator delete(void* p, std::align_val_t align) noexcept \
{ \
    RESOURCE_TYPE_NAME::get_memory_resource()->deallocate(p, sizeof(TYPE_NAME), static_cast<std::size_t>(align)); \
} \
 \
void operator delete(void* p, std::size_t size) noexcept \
{ \
    RESOURCE_TYPE_NAME::get_memory_resource()->deallocate(p, size, alignof(TYPE_NAME)); \
} \
 \
void operator delete(void* p, std::size_t size, std::align_val_t align) noexcept \
{ \
   RESOURCE_TYPE_NAME::get_memory_resource()->deallocate(p, size, static_cast<std::size_t>(align)); \
} \
\
void operator delete[](void* p) noexcept = delete; \
void operator delete[](void* p, std::align_val_t align) noexcept = delete; \
void operator delete[](void* p, std::size_t size) noexcept = delete; \
void operator delete[](void* p, std::size_t size, std::align_val_t align) noexcept = delete

#define BASE_DYNAMIC_MEMORY_HELPER(TYPE_NAME) \
DYNAMIC_MEMORY_GUARD_DECL \
DYNAMIC_MEMORY_HELPER(TYPE_NAME, TYPE_NAME)

#define DERIVED_DYNAMIC_MEMORY_HELPER(TYPE_NAME, RESOURCE_TYPE_NAME) \
DYNAMIC_MEMORY_GUARD_IMPL \
DYNAMIC_MEMORY_HELPER(TYPE_NAME, RESOURCE_TYPE_NAME)

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

TEST(StdX_MemoryResource_test_resource, destruction__no_destructor)
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

TEST(StdX_MemoryResource_test_resource, destruction__inconsistent_alignment)
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

TEST(StdX_MemoryResource_test_resource, destruction__wrong_number_of_bytes)
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
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U);
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

TEST(StdX_MemoryResource_test_resource, double_deallocation)
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
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U);
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
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0);
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

TEST(StdX_MemoryResource_test_resource, copy_assignment__incorrect)
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
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U); //deallocate actual buffer
    m_buffer = buff;
    strncpy(m_buffer, rhs.m_buffer, m_length);
    m_length = rhs.m_length;
    return *this;
  }

  ~pstring_correct_assignment_operator()
  {
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U);
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

TEST(StdX_MemoryResource_test_resource, self_assignment__incorrect)
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
      m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U); //deallocate actual buffer
      m_buffer = buff;
      strncpy(m_buffer, rhs.m_buffer, m_length);
      m_length = rhs.m_length;
    }
    return *this;
  }

  ~pstring_fixed_self_assignment()
  {
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U);
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

  pstring_with_move_constructor(pstring_with_move_constructor&& other, allocator_type allocator) noexcept
    : m_allocator(allocator) //don't propagate other allocator on extended move constructor
    , m_length(other.m_length)
  {
    if (m_allocator == other.m_allocator)
    {
      m_buffer = other.m_buffer;
      other.m_length = static_cast<std::size_t>(-1);
      other.m_buffer = nullptr;
    }
    else
    {
      m_buffer = nullptr;
      strncpy(m_buffer, other.m_buffer, m_length);
    }
  }

  //copy assignment operator
  pstring_with_move_constructor& operator=(const pstring_with_move_constructor& rhs)
  {
    if (this != std::addressof(rhs))
    {
      char* buff = m_allocator.allocate_object<char>(rhs.m_length + 1U); //create new buffer
      m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U); //deallocate actual buffer
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
    m_allocator.deallocate_object(m_buffer, m_length ? m_length + 1U : 0U);
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
    std::swap(m_length, other.m_length);
    std::swap(m_buffer, other.m_buffer);
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
  // alignment 1
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<1U>, stdx::pmr::detail::checked_alignment(1U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<1U>, sizeof(stdx::pmr::detail::header));

  // alignment 2
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<2U>, stdx::pmr::detail::checked_alignment(2U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<2U>, sizeof(stdx::pmr::detail::header));

  // alignment 4
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<4U>, stdx::pmr::detail::checked_alignment(4U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<4U>, sizeof(stdx::pmr::detail::header));

  // alignment 8
  EXPECT_EQ(stdx::pmr::detail::aligned_header_align_v<8U>, stdx::pmr::detail::checked_alignment(8U));
  EXPECT_EQ(stdx::pmr::detail::aligned_header_size_v<8U>, sizeof(stdx::pmr::detail::header));

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

TEST(StdX_MemoryResource_test_resource, overwrite_padding_before_payload)
{
  const bool verbose = g_verbose;
  stdx::pmr::test_resource dr("default", verbose);
  dr.set_no_abort(true);
  {
    pstring_correct astring{ "foobar", &dr };
    auto* ptr = astring.get_buffer() - 4U;
    *ptr = 0x65; //write 'e' - overwrite the tail padding area
  }
  EXPECT_EQ(dr.bounds_errors(), 1LL);
}

TEST(StdX_MemoryResource_test_resource, overwrite_padding_after_payload)
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

struct BaseEvent
{
  using allocator_type = stdx::pmr::polymorphic_allocator<>;
  using event_id_type = std::int32_t;
  using Ptr = std::shared_ptr<BaseEvent>;

  explicit BaseEvent(event_id_type eventType, [[maybe_unused]] allocator_type alloc = {}) noexcept
    : m_eventType(eventType)
  {
  }

  // copy constructor with allocator - extended copy constructor
  BaseEvent(const BaseEvent& other, [[maybe_unused]] allocator_type alloc = {}) noexcept
    : m_eventType{ other.m_eventType }
  {
  }

  // move constructor without allocator
  BaseEvent(BaseEvent&& other) noexcept
    : BaseEvent{ std::move(other), get_memory_resource() }
  {
  }

  // extended move constructor
  BaseEvent(BaseEvent&& other, [[maybe_unused]] allocator_type alloc) noexcept
    : m_eventType{ std::move(other.m_eventType) }
  {
  }

  //copy assignment operator
  BaseEvent& operator=(const BaseEvent& other) noexcept = default;

  //move assignment operator
  BaseEvent& operator=(BaseEvent&& other) noexcept
  {
    //assumption the same allocator - memory resource is used for all Events
    m_eventType = std::exchange(other.m_eventType, -1);
    return *this;
  }

  virtual ~BaseEvent() noexcept = default;

  static std::pmr::memory_resource* get_memory_resource()
  {
    static const bool verbose = g_verbose;
    static stdx::pmr::test_resource tr_default("BaseEvent: default_pool", verbose);
    tr_default.set_no_abort(true);
    static std::pmr::synchronized_pool_resource sync_pool(
      std::pmr::pool_options{ 0U, 4096U }, &tr_default);
    static stdx::pmr::test_resource resource("BaseEvent: sync_pool", verbose, &sync_pool);
    resource.set_no_abort(true);
    return &resource;
  }

  BASE_DYNAMIC_MEMORY_HELPER(BaseEvent);

private:

    event_id_type m_eventType;
};

template<typename E, typename... ARGS
  , std::enable_if_t<std::is_base_of_v<BaseEvent, E>>* = nullptr
>
[[nodiscard]]
static std::shared_ptr<E>
create_dynamic_shared(ARGS&&... args)
{
  auto* p = new E(std::forward<ARGS>(args)...);
  if (p)
  {
    // let internals of shared_ptr<E> be allocated via the Event memory_resource
    // and achieve a better locality
    std::shared_ptr<E> sp(p, std::default_delete<E>(), typename E::allocator_type(E::get_memory_resource()));
    return sp;
  }

  return { nullptr };
}

struct Event final : BaseEvent//, public MemoryHandler<Event>
{
  // for simplification just reuse the constructors of BaseEvent
  using BaseEvent::BaseEvent;

  Event(int l) : BaseEvent(1), level(l)
  {}

  Event& operator=(const Event&) = default;
  Event& operator=(Event&&) = default;

  int level;

  ~Event() override
  {
    //printf("%s\n", __FUNCTION__);
  }

  DERIVED_DYNAMIC_MEMORY_HELPER(Event, BaseEvent);
};

TEST(StdX_MemoryResource_test_resource, collection_test_resource)
{
  {
    const bool verbose = g_verbose;
    stdx::pmr::test_resource tr_default("default_pool", verbose);
    tr_default.set_no_abort(true);
    {
      std::pmr::synchronized_pool_resource pool{ std::pmr::pool_options{0U, 4096U}, &tr_default };
      stdx::pmr::test_resource tr_sync("sync_pool", verbose, &pool);
      tr_sync.set_no_abort(true);

      //printf("vector size: %zu\n", sizeof(std::pmr::vector<Event>));

      try
      {
        for (int i = 0; i < 10; ++i)
        {
          //printf("step: %d\n\n", i);
          std::pmr::vector<Event> coll{ &tr_sync };
          coll.reserve(100U);

          for (int j = 0; j < 100; ++j)
          {
            coll.emplace_back(j);
          }
        }

        //printf("--- leave scope of sync_pool\n");
      }
      catch (const stdx::pmr::test_resource_exception& e)
      {
        printf("test_resource_exception: size %zu alignment %zu\n", e.size(), e.alignment());
        throw;
      }

      EXPECT_FALSE(tr_sync.has_errors());
    }
    //printf("--- leave scope of default_pool\n");
    EXPECT_FALSE(tr_default.has_errors());
  } //deallocates all allocated memory
}

TEST(StdX_MemoryResource_test_resource, shared_pointer)
{
  //printf("size of BaseEvent: %zu\n", sizeof(BaseEvent));
  //printf("size of Event: %zu\n", sizeof(Event));

  stdx::pmr::test_resource_monitor trm(static_cast<stdx::pmr::test_resource&>(*Event::get_memory_resource()));
  std::pmr::vector<std::shared_ptr<Event>> v(Event::get_memory_resource());
  v.reserve(50U);

  for (int i = 0; i < 50; ++i)
  {
    trm.reset();
    v.emplace_back(create_dynamic_shared<Event>(i, Event::get_memory_resource()));
    //one memory block for shared_ptr_ctrl_block and one memory block for dynamic allocation of Event
    EXPECT_EQ(trm.delta_blocks_in_use(), 2LL);
  }
}
