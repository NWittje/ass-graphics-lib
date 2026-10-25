#include <cstdint>
#include <cstdlib>
#include <new>
namespace ag
{
thread_local uint64_t cpp_allocations = 0;
}
void *operator new(std::size_t size)
{
    if (void *p = std::malloc(size ? size : 1))
    {
        ++ag::cpp_allocations;
        return p;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}
