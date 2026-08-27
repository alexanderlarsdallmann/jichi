#ifndef BUFFER_HPP
#define BUFFER_HPP
#include <cstddef>

// A fixed-size buffer of ints that owns its storage. It works -- and it leaks:
// the constructor grabs heap memory with `new[]`, and nothing ever gives it
// back (there is no destructor). In C that leak is invisible; here the suite is
// built with AddressSanitizer, whose LeakSanitizer reports it at exit.
//
// This is a refactor: let the standard library OWN the memory (RAII), so there
// is no raw new/delete to get wrong. Change HOW, not WHAT.
class Buffer {
public:
    explicit Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    // BUG: no destructor -- the `new int[n]` above is never freed.
    int& at(std::size_t i) { return data_[i]; }
    std::size_t size() const { return size_; }

private:
    int* data_;
    std::size_t size_;
};

#endif
