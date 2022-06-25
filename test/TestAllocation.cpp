#include "memory_resource.h"

//  GTEST
#include <gtest/gtest.h>

#include <memory>
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
    : m_eventType{ other.m_eventType } // trivially-copyable type -> std::move has no effect
  {
  }

  //copy assignment operator
  BaseEvent& operator=(const BaseEvent& other) noexcept = default;

  //move assignment operator
  BaseEvent& operator=(BaseEvent&& other) noexcept
  {
      if (this != &other)
      {
          //assumption the same allocator - memory resource is used for all Events
          m_eventType = std::exchange(other.m_eventType, -1);
      }
  }

  virtual ~BaseEvent() noexcept = default;

  /**
   * \brief the Example of immortalization of resources
   *        the destructor of resources is not called automatically
   *        when the main function is finished, the memory occupied
   *        by resource objects is deallocated via destruction of buffers.
   *        However the memory blocks allocated by these resources are not
   *        deallocated automatically and the destructors on resources
   *        need to be invoked manually
   * \note  have a look at the function destruct_memory_resource()
   * \return pointer to memory_resource
   */
  static std::pmr::memory_resource* get_memory_resource()
  {
    static constexpr bool verbose = g_verbose;

    // the cascade of memory_resources
    //the 1sth resource
    alignas(stdx::pmr::test_resource) static std::uint8_t buffer_tr_default[sizeof(stdx::pmr::test_resource)];
    static auto* tr_default = new (buffer_tr_default) stdx::pmr::test_resource("BaseEvent: default_pool", verbose);
    tr_default->set_no_abort(true);

    //the 2nd
    alignas(std::pmr::synchronized_pool_resource) static std::uint8_t buffer_sync_pool[sizeof(std::pmr::synchronized_pool_resource)];
    static auto* sync_pool = new (buffer_sync_pool) std::pmr::synchronized_pool_resource(std::pmr::pool_options{ 0U, 4096U }, tr_default);

    //the 3rd
    alignas(stdx::pmr::test_resource) static std::uint8_t buffer_resource[sizeof(stdx::pmr::test_resource)];
    static auto* resource = new (buffer_resource) stdx::pmr::test_resource("BaseEvent: sync_pool", verbose, sync_pool);
    resource->set_no_abort(true);

    return resource;
  }

  // static allocation
  // the resources are destructed and deallocated when the main function
  // is finished automatically
  //static std::pmr::memory_resource* get_memory_resource()
  //{
  //  static constexpr bool verbose = g_verbose;
  //  static stdx::pmr::test_resource tr_default("BaseEvent: default_pool", verbose);
  //  tr_default.set_no_abort(true);
  //  static std::pmr::synchronized_pool_resource sync_pool(
  //    std::pmr::pool_options{ 0U, 4096U }, &tr_default);
  //  static stdx::pmr::test_resource resource("BaseEvent: sync_pool", verbose, &sync_pool);
  //  resource.set_no_abort(true);
  //  return &resource;
  //}

  /**
   * \brief Continuation of the Example of immortalization of resources;
   *        The kind of indication whether memory_resource object needs
   *        to be destructed is the definition of method release() by
   *        the corresponding memory_resource type (not all STD memory_resource types
   *        implement this method):
   *        https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource/release
   *        https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource/release
   *        https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource/release
   * \note  the function needs to be invoked before the end of main function
   *        to destruct memory resources
   */
  static void destruct_memory_resource()
  {
    auto* resource = dynamic_cast<stdx::pmr::test_resource*>(get_memory_resource());
    ASSERT_TRUE(resource != nullptr);
    auto* sync_pool = dynamic_cast<std::pmr::synchronized_pool_resource*>(resource->upstream_resource());
    ASSERT_TRUE(sync_pool != nullptr);
    auto* tr_default = dynamic_cast<stdx::pmr::test_resource*>(sync_pool->upstream_resource());
    ASSERT_TRUE(tr_default != nullptr);

    // the order how cascade of resources is destructed:
    // the last created resource (the 3rd) needs to be destructed as the first one
    std::destroy_at(resource);
    std::destroy_at(sync_pool);
    std::destroy_at(tr_default);
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
  if (auto* p = new E(std::forward<ARGS>(args)...); p)
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

  int level {-1};

  ~Event() override
  {
    //printf("%s\n", __FUNCTION__);
  }

  DERIVED_DYNAMIC_MEMORY_HELPER(Event, BaseEvent);
};

TEST(StdX_Allocation, collection_of_Event)
{
  {
    constexpr bool verbose = g_verbose;
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

TEST(StdX_Allocation, collection_of_shared_pointer_on_Event)
{
  //printf("size of BaseEvent: %zu\n", sizeof(BaseEvent));
  //printf("size of Event: %zu\n", sizeof(Event));

  {
    auto* resource = dynamic_cast<stdx::pmr::test_resource*>(Event::get_memory_resource());
    ASSERT_TRUE(resource != nullptr);
    stdx::pmr::test_resource_monitor trm(*resource);
    std::pmr::vector<std::shared_ptr<Event>> v(resource);
    v.reserve(50U);

    for (int i = 0; i < 50; ++i)
    {
      trm.reset();
      v.emplace_back(create_dynamic_shared<Event>(i, resource));
      //one memory block for shared_ptr_ctrl_block and one memory block for dynamic allocation of Event
      EXPECT_EQ(trm.delta_blocks_in_use(), 2LL);
    }
  }
  Event::destruct_memory_resource();
}
